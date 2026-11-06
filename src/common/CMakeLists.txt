# Request schema, payload framing and validators (the root helper's trust boundary),
# plus system readers shared by the app's probe and the helper.
# QtCore only, linked by the app and the helper.
qt_add_library(ekpo_common STATIC payload.cpp request.cpp sysinfo.cpp validate.cpp)
target_include_directories(ekpo_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ekpo_common PUBLIC Qt6::Core)
