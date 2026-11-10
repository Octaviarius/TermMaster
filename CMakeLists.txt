cmake_minimum_required(VERSION 3.12)

set(APP_VERSION_MAJOR 1 CACHE STRING "Major version")
set(APP_VERSION_MINOR 0 CACHE STRING "Minor version")
set(APP_VERSION_PATCH 0 CACHE STRING "Patch")
set(APP_VERSION_BUILD 0 CACHE STRING "Build number")

project(TermasterProject)

include(common.cmake)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
)
qt_standard_project_setup()

include(cmake/PluginApi.cmake)
include(cmake/Valijson.cmake)
include(cmake/Pcre2.cmake)
include(cmake/Fmt.cmake)

add_subdirectory(src)
add_subdirectory(components/yaml-cpp)

option(TERMASTER_BUILD_FORMAT_BENCHMARKS "Build formatting benchmarks" OFF)
if(TERMASTER_BUILD_FORMAT_BENCHMARKS)
    add_subdirectory(tools/benchmarks/Formatting)
endif()

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

option(TERMASTER_BUILD_COMMAND_TESTS "Build command lifecycle tests" OFF)
if(TERMASTER_BUILD_COMMAND_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Commands)
endif()

option(TERMASTER_BUILD_APP_TESTS "Build application shell tests" OFF)
if(TERMASTER_BUILD_APP_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/App)
endif()

option(TERMASTER_BUILD_WORKSPACE_TESTS "Build workspace and settings overlay tests" OFF)
if(TERMASTER_BUILD_WORKSPACE_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Workspace)
endif()

option(TERMASTER_BUILD_SESSION_TESTS "Build session document tests" OFF)
if(TERMASTER_BUILD_SESSION_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Session)
endif()

option(TERMASTER_BUILD_SCHEMA_TESTS "Build settings schema and generated form tests" OFF)
if(TERMASTER_BUILD_SCHEMA_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/SettingsSchema)
    add_subdirectory(tools/tests/SettingsForm)
endif()

option(TERMASTER_BUILD_TERMINAL_TESTS "Build terminal display and serial plugin tests" OFF)
if(TERMASTER_BUILD_TERMINAL_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/TerminalDisplay)
    add_subdirectory(tools/tests/Serial)
endif()

option(TERMASTER_BUILD_CONSOLE_TESTS "Build local console tests" OFF)
if(TERMASTER_BUILD_CONSOLE_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Console)
endif()

option(TERMASTER_BUILD_SOCKET_TESTS "Build socket terminal tests" OFF)
if(TERMASTER_BUILD_SOCKET_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Socket)
endif()

option(TERMASTER_BUILD_COMMAND_INPUT_TESTS "Build command preparation and output queue tests" OFF)
if(TERMASTER_BUILD_COMMAND_INPUT_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/CommandInput)
endif()

option(TERMASTER_BUILD_PROCESSING_TESTS "Build processor and Lineflow tests" OFF)
if(TERMASTER_BUILD_PROCESSING_TESTS)
    enable_testing()
    add_subdirectory(tools/tests/Processing)
    add_subdirectory(tools/tests/Formatting)
    add_subdirectory(tools/tests/ProcessingRouting)
    add_subdirectory(tools/tests/LineflowPersistence)
    add_subdirectory(tools/tests/LineflowEditor)
endif()

message("")
message("All project options table:")
show_all_options()
message("")

