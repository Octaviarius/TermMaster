cmake_minimum_required(VERSION 3.21)

project(RenderingProbe LANGUAGES CXX)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Widgets
    OpenGLWidgets
)

qt_add_executable(RenderingProbe
    Main.cpp
)

target_compile_features(RenderingProbe PRIVATE cxx_std_20)

target_link_libraries(RenderingProbe PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
)

if(WIN32)
    target_link_libraries(RenderingProbe PRIVATE
        psapi
    )
endif()
