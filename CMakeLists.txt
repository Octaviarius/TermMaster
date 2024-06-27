cmake_minimum_required(VERSION 3.5)
project(TermMasterProject)

option(TERMASTER_STATICALLY_LINK "Link application statically" OFF)

if(TERMASTER_STATICALLY_LINK)
    message("Will be statically linked")
    set(LIBRARIES_TYPE STATIC)
    set(YAML_BUILD_SHARED_LIBS OFF)
else()
    message("Will be dynamically linked")
    set(LIBRARIES_TYPE SHARED)
    set(YAML_BUILD_SHARED_LIBS ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

include_directories(
    ./src
    ./src/app
    ./src/lib
)

add_subdirectory(src/app)
add_subdirectory(src/lib)
