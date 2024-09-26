project(Terminal)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    SerialBus
    SerialPort
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    ITerminal.h
    ITerminal.cpp
    SerialTerminal.h
    SerialTerminal.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::SerialBus
    Qt::SerialPort
    Core
)
