project(CoreModule)
find_package(Qt6 COMPONENTS Core REQUIRED)

qt_add_library(CoreModule SHARED
    CoreModule.h
    CoreModule.cpp
)

set(LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core ExtensionSystem)
