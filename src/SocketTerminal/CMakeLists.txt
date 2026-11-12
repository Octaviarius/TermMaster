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

termaster_compile_help(
    TARGET SocketTerminalHelp
    NAMESPACE org.termaster.socketterminal
    OUTPUT_FILE "${CMAKE_BINARY_DIR}/plugin/SocketTerminal/${TERMASTER_PLUGIN_LIBRARY_DIR}/help/SocketTerminal.qch"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/Help/manifest.json"
    SCHEMAS
        "terminal.socket=${CMAKE_CURRENT_SOURCE_DIR}/Resources/Settings.yaml"
)
add_dependencies(SocketTerminal SocketTerminalHelp)
