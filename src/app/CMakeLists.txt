qt_add_library(personae_core STATIC importer.cpp library.cpp model.cpp probe.cpp)
target_include_directories(personae_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(personae_core PUBLIC Qt6::Core KF6::ConfigCore)

qt_add_executable(personae main.cpp)
target_compile_definitions(personae PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
qt_add_qml_module(personae
    URI Personae
    SOURCES backend.cpp backend.h
    QML_FILES qml/Main.qml qml/OverviewPage.qml qml/StagePage.qml qml/StatusLine.qml
)
target_link_libraries(personae PRIVATE personae_core Qt6::Gui Qt6::Quick Qt6::QuickControls2)
# The import scan only matters for static Qt builds; with the distro's shared Qt it
# just warns about plugin targets the -dev packages don't ship.
set_target_properties(personae PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
install(TARGETS personae)
