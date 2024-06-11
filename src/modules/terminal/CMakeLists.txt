project(TerminalModule)
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

qt_add_library(${PROJECT_NAME} SHARED
    TerminalModule.h
    TerminalModule.cpp
    TerminalModule.json
)

set(LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets yaml-cpp Utils ExtensionSystem)
