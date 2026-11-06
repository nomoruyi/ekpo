qt_add_library(ekpo_core STATIC cli.cpp executor.cpp importer.cpp library.cpp model.cpp planner.cpp probe.cpp target.cpp)
target_compile_definitions(ekpo_core PRIVATE EKPO_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/ekpo/ekpo-helper")
# Sandbox mode (EKPO_DEV + EKPO_SYSTEM_ROOT): the test helper and its stub commands.
# Packages are built with -DEKPO_SANDBOX=OFF (top-level option), so the installed
# app has no dev hook.
if(EKPO_SANDBOX)
    target_compile_definitions(ekpo_core PRIVATE EKPO_SANDBOX
        EKPO_HELPER_TEST_PATH="$<TARGET_FILE:ekpo-helper-test>"
        EKPO_STUBS_DIR="${CMAKE_SOURCE_DIR}/tests/stubs")
endif()
target_include_directories(ekpo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ekpo_core PUBLIC ekpo_common Qt6::Core KF6::ConfigCore)

qt_add_executable(ekpo main.cpp)
target_compile_definitions(ekpo PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
qt_add_qml_module(ekpo
    URI Ekpo
    SOURCES backend.cpp backend.h
    QML_FILES qml/Main.qml qml/OverviewPage.qml qml/StagePage.qml qml/StatusLine.qml qml/ReviewPage.qml qml/HistoryPage.qml qml/MasksPage.qml qml/IntroDialog.qml qml/SettingsPage.qml qml/AboutPage.qml
)
target_link_libraries(ekpo PRIVATE ekpo_core Qt6::DBus Qt6::Gui Qt6::Network Qt6::Quick Qt6::QuickControls2)
# The import scan only matters for static Qt builds; with the distro's shared Qt it
# just warns about plugin targets the -dev packages don't ship.
set_target_properties(ekpo PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
qt_add_resources(ekpo "icon" PREFIX "/" BASE ${CMAKE_SOURCE_DIR}/packaging
    FILES ${CMAKE_SOURCE_DIR}/packaging/io.github.nomoruyi.ekpo.svg)
install(TARGETS ekpo)
