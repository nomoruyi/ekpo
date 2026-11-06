cmake_minimum_required(VERSION 3.28)

# Release versions come from git tags (vX.Y.Z), which CI creates from "[major]",
# "[minor]" or "[patch]" in a commit message and passes as -DEKPO_VERSION.
# Local builds use the latest tag, or the fallback before the first release.
set(EKPO_VERSION "" CACHE STRING "Version to build (default: latest vX.Y.Z git tag)")
set(ekpo_version "${EKPO_VERSION}")
if(NOT ekpo_version)
    execute_process(COMMAND git describe --tags --abbrev=0 --match "v[0-9]*.[0-9]*.[0-9]*"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE ekpo_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE ekpo_git_result)
    if(ekpo_git_result EQUAL 0)
        string(REGEX REPLACE "^v" "" ekpo_version "${ekpo_tag}")
    else()
        set(ekpo_version 0.2.0)
    endif()
endif()
project(ekpo VERSION ${ekpo_version} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Gui Network Qml Quick QuickControls2 Test)
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
