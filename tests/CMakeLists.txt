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

# Sandbox screenshots of the stage pages: the reference fixture as the system and a
# copy of tests/fixtures/library (three Boot Splash imports) as the library.
set(SAMPLE_DATA ${CMAKE_BINARY_DIR}/sample-data)
add_test(NAME prepare_sample_library COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/library ${SAMPLE_DATA}/ekpo)
set_tests_properties(prepare_sample_library PROPERTIES FIXTURES_SETUP sample_library)
foreach(section bootMenu bootSplash plasmaSplash masks history)
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

add_test(NAME smoke_review COMMAND ekpo)
set_tests_properties(smoke_review PROPERTIES FIXTURES_REQUIRED sample_library ENVIRONMENT
    "${DEV_ENV};XDG_DATA_HOME=${SAMPLE_DATA};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;EKPO_REVIEW_IMPORT=00000000-0000-0000-0000-000000000004;EKPO_SYSTEM_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference;EKPO_SYSTEM_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roots/reference/home;EKPO_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/review.png")

ekpo_test(tst_probe)
ekpo_test(tst_library)
ekpo_test(tst_importer)
ekpo_test(tst_common)
ekpo_test(tst_helper)
add_dependencies(tst_helper ekpo-helper ekpo-helper-test)
ekpo_test(tst_executor)
add_dependencies(tst_executor ekpo-helper-test)
target_compile_definitions(tst_executor PRIVATE
    HELPER_TEST_BIN="$<TARGET_FILE:ekpo-helper-test>"
    STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_compile_definitions(tst_helper PRIVATE
    HELPER_BIN="$<TARGET_FILE:ekpo-helper>"
    HELPER_TEST_BIN="$<TARGET_FILE:ekpo-helper-test>"
    STUBS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
