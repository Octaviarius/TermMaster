find_package(Qt6 REQUIRED COMPONENTS
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
        Qt6::Widgets
    )
    if(WIN32)
        target_compile_definitions(${console_test} PRIVATE
            _WIN32_WINNT=0x0A00
        )
        target_link_libraries(${console_test} PRIVATE
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
    ../../../src/ConsoleTerminal/src/ConsoleDevice.cpp
    ../../../src/ConsoleTerminal/src/ConsoleTerminal.cpp
    ../../../src/ConsoleTerminal/src/ConsolePlugin.cpp
    ../../../src/ConsoleTerminal/src/ConsolePlugin.h
    ../../../src/ConsoleTerminal/Resources/ConsoleTerminal.qrc
)
set_target_properties(ConsolePluginTests PROPERTIES
    AUTORCC ON
)
