project(Terminal)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    SerialBus
    SerialPort
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Terminal/ITerminal.h
    src/ITerminal.cpp
    inc/Termaster/Terminal/SerialTerminal.h
    src/SerialTerminal.cpp
    inc/Termaster/Terminal/Emulator.h
    src/Emulator.cpp

)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::SerialBus
    Qt::SerialPort
    Core
    Widgets
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
