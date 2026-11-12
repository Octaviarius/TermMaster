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
        src/Gui/Serial/PortSelector.cpp
        src/Gui/Serial/BaudRateSelector.cpp
        src/Gui/Serial/PortSelector.h
        Resources/SerialTerminal.qrc
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

termaster_compile_help(
    TARGET SerialTerminalHelp
    NAMESPACE org.termaster.serialterminal
    OUTPUT_FILE "${CMAKE_BINARY_DIR}/plugin/SerialTerminal/${TERMASTER_PLUGIN_LIBRARY_DIR}/help/SerialTerminal.qch"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/Help/manifest.json"
    SCHEMAS
        "terminal.serial=${CMAKE_CURRENT_SOURCE_DIR}/Resources/Settings.yaml"
)
add_dependencies(SerialTerminal SerialTerminalHelp)
