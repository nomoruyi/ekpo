file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/screenshots)

# Unit tests get the repo-local home, XDG dirs and TMPDIR even when run without
# dev/env.sh, so nothing they write (temp dirs, the trash) leaves the repo.
set(DEV_DIR ${CMAKE_SOURCE_DIR}/.dev)
set(DEV_HOME ${DEV_DIR}/home)
set(DEV_ENV
    "EKPO_DEV=${DEV_DIR}" "HOME=${DEV_HOME}" "TMPDIR=${DEV_DIR}/tmp"
    "XDG_CONFIG_HOME=${DEV_HOME}/.config" "XDG_DATA_HOME=${DEV_HOME}/.local/share"
    "XDG_CACHE_HOME=${DEV_HOME}/.cache" "XDG_STATE_HOME=${DEV_HOME}/.local/state")
file(MAKE_DIRECTORY ${DEV_DIR}/tmp ${DEV_HOME}/.config ${DEV_HOME}/.local/share ${DEV_HOME}/.cache ${DEV_HOME}/.local/state)

function(ekpo_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ekpo_core ekpo_common Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        DEV_DIR="${DEV_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${DEV_ENV}")
endfunction()

# Smoke test: the main window loads without any QML warning and can be rendered.
add_test(NAME smoke_main_window COMMAND ekpo)
set_tests_properties(smoke_main_window PROPERTIES ENVIRONMENT
    "${DEV_ENV};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/main.png")

# QML tests (tests/qml, qmltestrunner) in the KDE style the app uses.
find_program(QMLTESTRUNNER qmltestrunner HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" REQUIRED)
add_test(NAME qml_tests COMMAND ${QMLTESTRUNNER} -input ${CMAKE_CURRENT_SOURCE_DIR}/qml)
set_tests_properties(qml_tests PROPERTIES ENVIRONMENT
    "${DEV_ENV};QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=org.kde.desktop;QT_FATAL_WARNINGS=1")

# A fake root without EKPO_DEV must stop the app instead of falling back to the real
# system. EKPO_DEV is unset explicitly: ctest inherits it from dev/env.sh. (status
# only reads, so even a broken guard can't change anything.)
set(NO_DEV_ENV ${DEV_ENV})
list(FILTER NO_DEV_ENV EXCLUDE REGEX "^EKPO_DEV=")
add_test(NAME sandbox_needs_dev COMMAND ekpo status)
set_tests_properties(sandbox_needs_dev PROPERTIES ENVIRONMENT_MODIFICATION "EKPO_DEV=unset:"
    PASS_REGULAR_EXPRESSION "EKPO_DEV isn't" ENVIRONMENT
    "${NO_DEV_ENV};EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home")
add_test(NAME sandbox_needs_dev_gui COMMAND ekpo)
set_tests_properties(sandbox_needs_dev_gui PROPERTIES ENVIRONMENT_MODIFICATION "EKPO_DEV=unset:"
    TIMEOUT 30 PASS_REGULAR_EXPRESSION "EKPO_DEV isn't" ENVIRONMENT
    "${NO_DEV_ENV};QT_QPA_PLATFORM=offscreen;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home")

# Sandbox screenshots of the stage pages: the reference fixture as the system and a
# copy of tests/fixtures/library (three Boot Splash imports) as the library.
set(SAMPLE_DATA ${CMAKE_BINARY_DIR}/sample-data)
add_test(NAME prepare_sample_library COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/library ${SAMPLE_DATA}/ekpo)
set_tests_properties(prepare_sample_library PROPERTIES FIXTURES_SETUP sample_library)
foreach(section bootMenu bootSplash plasmaSplash masks assets history settings about)
    add_test(NAME smoke_${section} COMMAND ekpo)
    set_tests_properties(smoke_${section} PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
        "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_SECTION=${section};EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/${section}.png")
endforeach()

# The after-reboot question: the sample library plus a pending check from an earlier boot.
add_test(NAME prepare_check_library COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/library ${CMAKE_BINARY_DIR}/check-data/ekpo)
add_test(NAME prepare_check_file COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pending-check.json ${CMAKE_BINARY_DIR}/check-data/ekpo/pending-check.json)
set_tests_properties(prepare_check_library PROPERTIES FIXTURES_SETUP check_library)
set_tests_properties(prepare_check_file PROPERTIES FIXTURES_SETUP check_library DEPENDS prepare_check_library)
add_test(NAME smoke_check COMMAND ekpo)
set_tests_properties(smoke_check PROPERTIES FIXTURES_REQUIRED check_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${CMAKE_BINARY_DIR}/check-data;QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/check.png")

# The editor on the sample design "Dawn".
add_test(NAME smoke_editor COMMAND ekpo)
set_tests_properties(smoke_editor PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_EDIT_DESIGN=00000000-0000-0000-0000-0000000000d1;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/editor.png")

add_test(NAME smoke_editor_boot COMMAND ekpo)
set_tests_properties(smoke_editor_boot PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_EDIT_DESIGN=00000000-0000-0000-0000-0000000000d2;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/editor-boot.png")

add_test(NAME smoke_editor_grub COMMAND ekpo)
set_tests_properties(smoke_editor_grub PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_EDIT_DESIGN=00000000-0000-0000-0000-0000000000d3;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/editor-grub.png")

add_test(NAME smoke_review COMMAND ekpo)
set_tests_properties(smoke_review PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_REVIEW_IMPORT=00000000-0000-0000-0000-000000000005;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/review.png")

# Every page in turn, then the boot menu editor opened and left twice: Kirigami warns
# when a page comes back into the row the wrong way.
add_test(NAME smoke_tour COMMAND ekpo)
set_tests_properties(smoke_tour PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_TOUR=1;EKPO_EDIT_DESIGN=00000000-0000-0000-0000-0000000000d3;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/tour.png")

# A newer release found at start (a file:// fixture instead of GitHub): the Overview's
# banner and the update dialog with the release notes.
add_test(NAME smoke_update COMMAND ekpo)
set_tests_properties(smoke_update PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_UPDATE_URL=file://${CMAKE_CURRENT_SOURCE_DIR}/fixtures/updates/release.json;EKPO_UPDATE_DIALOG=1;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/update.png")

# Report a Bug from the Overview: the dialog with its screenshot preview.
add_test(NAME smoke_bug_report COMMAND ekpo)
set_tests_properties(smoke_bug_report PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_BUG_REPORT=1;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/bugReport.png")

ekpo_test(tst_probe)
ekpo_test(tst_updater)
target_link_libraries(tst_updater PRIVATE Qt6::Network)
target_compile_definitions(tst_updater PRIVATE STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
ekpo_test(tst_library)
ekpo_test(tst_importer)
ekpo_test(tst_media)
ekpo_test(tst_pf2)
ekpo_test(tst_gengrub)
set_tests_properties(tst_gengrub PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen")
ekpo_test(tst_genplymouth)
set_tests_properties(tst_genplymouth PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen")
ekpo_test(tst_genksplash)
target_link_libraries(tst_genksplash PRIVATE Qt6::Gui Qt6::Quick)
set_target_properties(tst_genksplash PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE) # as for the app (src/app/CMakeLists.txt)
set_tests_properties(tst_genksplash PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen")
# The editor is part of the app's QML module; the test compiles it in directly.
ekpo_test(tst_designeditor)
target_sources(tst_designeditor PRIVATE ${CMAKE_SOURCE_DIR}/src/app/designeditor.cpp ${CMAKE_SOURCE_DIR}/src/app/designeditor.h)
target_link_libraries(tst_designeditor PRIVATE Qt6::Gui Qt6::Qml)
set_target_properties(tst_designeditor PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
set_tests_properties(tst_designeditor PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen")
ekpo_test(tst_bugreport)
target_link_libraries(tst_bugreport PRIVATE Qt6::Gui)
set_tests_properties(tst_bugreport PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen")
ekpo_test(tst_common)
ekpo_test(tst_helper)
add_dependencies(tst_helper ekpo-helper ekpo-helper-test)
ekpo_test(tst_cli)
add_dependencies(tst_cli ekpo ekpo-helper-test)
target_compile_definitions(tst_cli PRIVATE EKPO_BIN="$<TARGET_FILE:ekpo>" STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
ekpo_test(tst_executor)
set_tests_properties(tst_executor PROPERTIES ENVIRONMENT "${DEV_ENV};QT_QPA_PLATFORM=offscreen") # boot splash designs draw text
add_dependencies(tst_executor ekpo-helper-test)
target_compile_definitions(tst_executor PRIVATE
    HELPER_TEST_BIN="$<TARGET_FILE:ekpo-helper-test>"
    STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_compile_definitions(tst_helper PRIVATE
    HELPER_BIN="$<TARGET_FILE:ekpo-helper>"
    HELPER_TEST_BIN="$<TARGET_FILE:ekpo-helper-test>"
    STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
