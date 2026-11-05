file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/screenshots)

# Smoke test: the main window loads without any QML warning and can be rendered.
add_test(NAME smoke_main_window COMMAND personae)
set_tests_properties(smoke_main_window PROPERTIES ENVIRONMENT
    "PERSONAE_DEV=1;QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;PERSONAE_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/main.png")

function(personae_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE personae_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

personae_test(tst_probe)
