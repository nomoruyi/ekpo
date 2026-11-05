# Smoke test: the main window loads without any QML warning and can be rendered.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/screenshots)
add_test(NAME smoke_main_window COMMAND personae)
set_tests_properties(smoke_main_window PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_FATAL_WARNINGS=1;PERSONAE_SCREENSHOT=${CMAKE_BINARY_DIR}/screenshots/main.png")
