# The root helper. ekpo-helper is what gets installed; ekpo-helper-test is the same
# code built to work on a fake root with stub commands, for tests only.
foreach(target ekpo-helper ekpo-helper-test)
    qt_add_executable(${target} main.cpp helper.cpp)
    target_link_libraries(${target} PRIVATE ekpo_common Qt6::Core)
endforeach()
target_compile_definitions(ekpo-helper-test PRIVATE EKPO_HELPER_TESTING)
install(TARGETS ekpo-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/ekpo)
