find_package(Qt6 REQUIRED COMPONENTS
    Network
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
        src/Gui/Launch/ShellSelector.cpp
        src/Gui/Launch/ShellSelector.h
        Resources/ConsoleTerminal.qrc
    PUBLIC_HEADERS
        inc/Termaster/ConsoleTerminal/ConsoleTerminal.h
)
set_target_properties(ConsoleTerminal PROPERTIES
    AUTORCC ON
)
target_link_libraries(ConsoleTerminal PRIVATE
    Qt6::Network
    Qt6::Widgets
)
if(WIN32)
    target_sources(ConsoleTerminal PRIVATE
        src/Elevation/ElevatedConsoleDevice.cpp
        src/Elevation/ElevatedConsoleDevice.h
        src/Elevation/ElevationProtocol.cpp
        src/Elevation/ElevationProtocol.h
    )
    target_compile_definitions(ConsoleTerminal PRIVATE
        _WIN32_WINNT=0x0A00
    )
    target_link_libraries(ConsoleTerminal PRIVATE
        shell32
        ole32
        userenv
        advapi32
    )
endif()

if(WIN32)
    set(TERMASTER_CONSOLE_HOST_ICON "${CMAKE_SOURCE_DIR}/assets/IconOrange.ico")
    configure_file(Resources/ConsoleHost.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/ConsoleHost.rc"
        @ONLY
    )
    qt_add_executable(ConsoleHost WIN32
        src/Elevation/ConsoleHost.cpp
        src/Elevation/ElevationProtocol.cpp
        src/Elevation/ElevationProtocol.h
        src/ConsoleDevice.cpp
        src/ConsoleDevice.h
        src/ConsoleEnvironment.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/ConsoleHost.rc"
    )
    target_compile_features(ConsoleHost PRIVATE cxx_std_20)
    target_compile_definitions(ConsoleHost PRIVATE
        TERMASTER_CONSOLE_HOST
        _WIN32_WINNT=0x0A00
    )
    target_include_directories(ConsoleHost PRIVATE
        src
        ../Core/inc
    )
    target_link_libraries(ConsoleHost PRIVATE
        Qt6::Core
        Qt6::Network
        userenv
        advapi32
    )
    set_target_properties(ConsoleHost PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Helpers"
    )
    add_custom_command(TARGET ConsoleHost POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>"
            "$<TARGET_FILE:Qt6::Network>"
            "$<TARGET_FILE_DIR:ConsoleHost>"
        VERBATIM
    )
    add_dependencies(ConsoleTerminal ConsoleHost)
endif()
