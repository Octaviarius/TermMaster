cmake_minimum_required(VERSION 3.5)
project(MainProject)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

include_directories(
    ./src
    ./src/lib
)

add_subdirectory(src/app)
add_subdirectory(src/lib)
add_subdirectory(src/modules)
