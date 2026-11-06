cmake_minimum_required(VERSION 3.28)
project(ekpo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
find_package(KF6Config 6.0 REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

include(GNUInstallDirs)
# Installed folders are 0755 whatever the builder's umask.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
# OFF for packages: no sandbox hook in the app, and no tests, since without the
# sandbox the CLI and app tests would reach the real helper through pkexec.
option(EKPO_SANDBOX "Build the sandbox mode and the tests" ON)
add_subdirectory(src/common)
add_subdirectory(src/helper)
add_subdirectory(src/app)
add_subdirectory(packaging)
if(EKPO_SANDBOX)
    enable_testing()
    add_subdirectory(tests)
endif()
