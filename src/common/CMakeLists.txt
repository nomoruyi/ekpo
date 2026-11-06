# Request schema, payload framing and validators: the root helper's trust boundary.
# QtCore only, linked by the app and the helper.
qt_add_library(ekpo_common STATIC payload.cpp request.cpp validate.cpp)
target_include_directories(ekpo_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ekpo_common PUBLIC Qt6::Core)
