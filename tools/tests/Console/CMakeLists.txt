find_package(Qt6 REQUIRED COMPONENTS
    Network
    Test
    Widgets
)
add_executable(ConsoleChild
    ConsoleChild.cpp
)
target_compile_features(ConsoleChild PRIVATE cxx_std_20)
set_target_properties(ConsoleChild PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
)
foreach(console_test IN ITEMS
    ConsoleDeviceTests
    ConsoleEnvironmentTests
    ConsolePluginTests
    ConsoleHostProtocolTests
)
    qt_add_executable(${console_test}
        ${console_test}.cpp
        ../../../src/ConsoleTerminal/src/ConsoleEnvironment.cpp
        ../../../src/ConsoleTerminal/src/Gui/Environment/EnvironmentEditor.cpp
    )
    target_compile_features(${console_test} PRIVATE cxx_std_20)
    target_include_directories(${console_test} PRIVATE
        ../../../src/ConsoleTerminal/src
        ../../../src/ConsoleTerminal/inc
    )
    target_link_libraries(${console_test} PRIVATE
        Termaster::PluginApi
        Qt6::Test
        Qt6::Network
        Qt6::Widgets
    )
    if(WIN32)
        target_compile_definitions(${console_test} PRIVATE
            _WIN32_WINNT=0x0A00
        )
        target_link_libraries(${console_test} PRIVATE
            shell32
            ole32
            userenv
            advapi32
        )
    endif()
    add_test(NAME ${console_test} COMMAND ${console_test})
    set_tests_properties(${console_test} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endforeach()
target_sources(ConsoleDeviceTests PRIVATE
    ../../../src/ConsoleTerminal/src/ConsoleDevice.cpp
)
add_dependencies(ConsoleDeviceTests ConsoleChild)
target_sources(ConsolePluginTests PRIVATE
    ../../../src/ConsoleTerminal/src/Gui/Launch/ShellSelector.cpp
    ../../../src/ConsoleTerminal/src/ConsoleDevice.cpp
    ../../../src/ConsoleTerminal/src/ConsoleTerminal.cpp
    ../../../src/ConsoleTerminal/src/ConsolePlugin.cpp
    ../../../src/ConsoleTerminal/src/ConsolePlugin.h
    ../../../src/ConsoleTerminal/Resources/ConsoleTerminal.qrc
)
set_target_properties(ConsolePluginTests PROPERTIES
    AUTORCC ON
)

if(WIN32)
    foreach(console_device_test IN ITEMS
        ConsoleDeviceTests
        ConsolePluginTests
    )
        target_sources(${console_device_test} PRIVATE
            ../../../src/ConsoleTerminal/src/Elevation/ElevatedConsoleDevice.cpp
            ../../../src/ConsoleTerminal/src/Elevation/ElevationProtocol.cpp
        )
    endforeach()
    qt_add_executable(ConsoleElevationTests
        ConsoleElevationTests.cpp
        ../../../src/ConsoleTerminal/src/Elevation/ElevatedConsoleDevice.cpp
        ../../../src/ConsoleTerminal/src/Elevation/ElevationProtocol.cpp
    )
    target_compile_features(ConsoleElevationTests PRIVATE cxx_std_20)
    target_compile_definitions(ConsoleElevationTests PRIVATE
        _WIN32_WINNT=0x0A00
    )
    target_include_directories(ConsoleElevationTests PRIVATE
        ../../../src/ConsoleTerminal/src
    )
    target_link_libraries(ConsoleElevationTests PRIVATE
        Termaster::PluginApi
        Qt6::Network
        Qt6::Test
        shell32
        ole32
    )
    add_dependencies(ConsoleElevationTests ConsoleHost ConsoleChild)
    add_test(NAME ConsoleElevationTests COMMAND ConsoleElevationTests)
    set_tests_properties(ConsoleElevationTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endif()
target_sources(ConsoleHostProtocolTests PRIVATE
    ../../../src/ConsoleTerminal/src/Elevation/ElevationProtocol.cpp
)
