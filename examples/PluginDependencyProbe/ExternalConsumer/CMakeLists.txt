cmake_minimum_required(VERSION 3.21)

project(ExternalPluginConsumer LANGUAGES CXX)

find_package(PluginDependencyProbe 1 CONFIG REQUIRED)
qt_standard_project_setup()

qt_add_plugin(Consumer
    CLASS_NAME ExternalConsumer
    ExternalConsumer.h
    ExternalConsumer.cpp
)

target_compile_features(Consumer PRIVATE cxx_std_20)
target_link_libraries(Consumer PRIVATE PluginDependencyProbe::Provider)
set_target_properties(Consumer PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
)
