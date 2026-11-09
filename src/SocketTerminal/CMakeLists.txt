find_package(Qt6 REQUIRED COMPONENTS
    Network
    Widgets
)
termaster_add_plugin(SocketTerminal
    SOURCES
        src/SocketTerminal.cpp
        src/SocketDevice.cpp
        src/SocketDevice.h
        src/SocketPlugin.cpp
        src/SocketPlugin.h
        Resources/SocketTerminal.qrc
    PUBLIC_HEADERS
        inc/Termaster/SocketTerminal/SocketTerminal.h
)
set_target_properties(SocketTerminal PROPERTIES
    AUTORCC ON
)
target_link_libraries(SocketTerminal PRIVATE
    Qt6::Network
    Qt6::Widgets
)
