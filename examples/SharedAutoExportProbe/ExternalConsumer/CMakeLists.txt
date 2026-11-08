cmake_minimum_required(VERSION 3.21)

project(ExternalAutoExportConsumer LANGUAGES CXX)

find_package(SharedAutoExportProbe 1 CONFIG REQUIRED)
qt_standard_project_setup()

add_library(Consumer SHARED
    ExternalConsumer.h
    ExternalConsumer.cpp
)
target_compile_features(Consumer PRIVATE cxx_std_20)
target_link_libraries(Consumer PRIVATE SharedAutoExportProbe::Provider)
set_target_properties(Consumer PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
)
