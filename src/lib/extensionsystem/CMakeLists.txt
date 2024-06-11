project(ExtensionSystem)
find_package(Qt6 COMPONENTS Core REQUIRED)

qt_add_library(${PROJECT_NAME} SHARED
    Module.h
    ModuleManager.h ModuleManager.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Utils)
