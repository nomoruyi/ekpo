file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/screenshots)

# Unit tests get the repo-local home, XDG dirs and TMPDIR even when run without
# dev/env.sh, so nothing they write (temp dirs, the trash) leaves the repo.
set(DEV_DIR ${CMAKE_SOURCE_DIR}/.dev)
set(DEV_HOME ${DEV_DIR}/home)
set(DEV_ENV
    "PERSONAE_DEV=${DEV_DIR}" "HOME=${DEV_HOME}" "TMPDIR=${DEV_DIR}/tmp"
    "XDG_CONFIG_HOME=${DEV_HOME}/.config" "XDG_DATA_HOME=${DEV_HOME}/.local/share"
    "XDG_CACHE_HOME=${DEV_HOME}/.cache" "XDG_STATE_HOME=${DEV_HOME}/.local/state")
file(MAKE_DIRECTORY ${DEV_DIR}/tmp ${DEV_HOME}/.config ${DEV_HOME}/.local/share ${DEV_HOME}/.cache ${DEV_HOME}/.local/state)

function(personae_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE personae_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        DEV_DIR="${DEV_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${DEV_ENV}")
endfunction()

# Smoke test: the main window loads without any QML warning and can be rendered.
add_test(NAME smoke_main_window COMMAND personae)
set_tests_properties(smoke_main_window PROPERTIES ENVIRONMENT
    "${DEV_ENV};QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;PERSONAE_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/main.png")

personae_test(tst_probe)
personae_test(tst_library)
personae_test(tst_importer)
