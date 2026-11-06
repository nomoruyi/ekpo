qt_add_library(ekpo_core STATIC importer.cpp library.cpp model.cpp probe.cpp)
target_include_directories(ekpo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ekpo_core PUBLIC ekpo_common Qt6::Core KF6::ConfigCore)

qt_add_executable(ekpo main.cpp)
target_compile_definitions(ekpo PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
qt_add_qml_module(ekpo
    URI Ekpo
    SOURCES backend.cpp backend.h
    QML_FILES qml/Main.qml qml/OverviewPage.qml qml/StagePage.qml qml/StatusLine.qml
)
target_link_libraries(ekpo PRIVATE ekpo_core Qt6::Gui Qt6::Quick Qt6::QuickControls2)
# The import scan only matters for static Qt builds; with the distro's shared Qt it
# just warns about plugin targets the -dev packages don't ship.
set_target_properties(ekpo PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
qt_add_resources(ekpo "icon" PREFIX "/" BASE ${CMAKE_SOURCE_DIR}/packaging
    FILES ${CMAKE_SOURCE_DIR}/packaging/io.github.nomoruyi.ekpo.svg)
install(TARGETS ekpo)
