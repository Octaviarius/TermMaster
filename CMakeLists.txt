cmake_minimum_required(VERSION 3.12)

set(APP_VERSION_MAJOR 1 CACHE STRING "Major version")
set(APP_VERSION_MINOR 0 CACHE STRING "Minor version")
set(APP_VERSION_PATCH 0 CACHE STRING "Patch")
set(APP_VERSION_BUILD 0 CACHE STRING "Build number")

project(TermMasterProject)

include(common.cmake)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

add_subdirectory(src/app)
add_subdirectory(src/lib/yaml-cpp)

message("")
message("All project options table:")
show_all_options()
message("")

