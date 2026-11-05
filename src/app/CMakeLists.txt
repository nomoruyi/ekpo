qt_add_executable(personae main.cpp)
target_compile_definitions(personae PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
qt_add_qml_module(personae
    URI Personae
    QML_FILES qml/Main.qml
)
target_link_libraries(personae PRIVATE Qt6::Gui Qt6::Quick Qt6::QuickControls2)
install(TARGETS personae)
