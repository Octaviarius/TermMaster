find_package(Qt6 REQUIRED COMPONENTS
    SerialPort
    Widgets
)
termaster_add_plugin(SerialTerminal
    SOURCES
        src/SerialTerminal.cpp
        src/PortClassification.cpp
        src/PortClassification.h
        src/SerialPlugin.cpp
        src/SerialPlugin.h
        src/SerialByteDevice.cpp
        src/SerialByteDevice.h
        src/gui/serial/PortSelector.cpp
        src/gui/serial/PortSelector.h
        resources/SerialTerminal.qrc
    PUBLIC_HEADERS
        inc/Termaster/SerialTerminal/SerialTerminal.h
)
set_target_properties(SerialTerminal PROPERTIES
    AUTORCC ON
)
target_link_libraries(SerialTerminal PRIVATE
    Qt6::SerialPort
    Qt6::Widgets
)
if(WIN32)
    target_link_libraries(SerialTerminal PRIVATE
        setupapi
        cfgmgr32
        advapi32
    )
endif()
