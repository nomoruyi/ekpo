cmake_minimum_required(VERSION 3.28)
project(personae VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
find_package(KF6Config 6.0 REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()
add_subdirectory(src/app)
add_subdirectory(tests)
