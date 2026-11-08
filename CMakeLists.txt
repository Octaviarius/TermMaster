cmake_minimum_required(VERSION 3.12)

set(APP_VERSION_MAJOR 1 CACHE STRING "Major version")
set(APP_VERSION_MINOR 0 CACHE STRING "Minor version")
set(APP_VERSION_PATCH 0 CACHE STRING "Patch")
set(APP_VERSION_BUILD 0 CACHE STRING "Build number")

project(TermasterProject)

include(common.cmake)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

include(cmake/PluginApi.cmake)

add_subdirectory(src)
add_subdirectory(components/yaml-cpp)

option(TERMASTER_BUILD_PLUGIN_TESTS "Build plugin lifecycle integration tests" OFF)
if(TERMASTER_BUILD_PLUGIN_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Plugin)
endif()

option(TERMASTER_BUILD_ACTION_TESTS "Build action container tests" OFF)
if(TERMASTER_BUILD_ACTION_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/ActionContainers)
endif()

option(TERMASTER_BUILD_PATH_TESTS "Build lexical and filesystem path tests" OFF)
if(TERMASTER_BUILD_PATH_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Paths)
endif()

option(TERMASTER_BUILD_SETTINGS_TESTS "Build YAML settings tests" OFF)
if(TERMASTER_BUILD_SETTINGS_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Settings)
endif()

message("")
message("All project options table:")
show_all_options()
message("")

