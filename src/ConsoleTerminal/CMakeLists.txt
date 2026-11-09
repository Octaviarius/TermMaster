find_package(Qt6 REQUIRED COMPONENTS
    Widgets
)
termaster_add_plugin(ConsoleTerminal
    SOURCES
        src/ConsoleTerminal.cpp
        src/ConsoleDevice.cpp
        src/ConsoleDevice.h
        src/ConsoleEnvironment.cpp
        src/ConsoleEnvironment.h
        src/ConsolePlugin.cpp
        src/ConsolePlugin.h
        src/Gui/Environment/EnvironmentEditor.cpp
        src/Gui/Environment/EnvironmentEditor.h
        Resources/ConsoleTerminal.qrc
    PUBLIC_HEADERS
        inc/Termaster/ConsoleTerminal/ConsoleTerminal.h
)
set_target_properties(ConsoleTerminal PROPERTIES
    AUTORCC ON
)
target_link_libraries(ConsoleTerminal PRIVATE
    Qt6::Widgets
)
if(WIN32)
    target_compile_definitions(ConsoleTerminal PRIVATE
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(ConsoleTerminal PRIVATE
        userenv
        advapi32
    )
endif()
