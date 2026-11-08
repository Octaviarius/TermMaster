find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
    SerialPort
)
qt_add_executable(SerialDeviceTests
    ../../../src/serialterminal/resources/SerialTerminal.qrc
    SerialDeviceTests.cpp
    ../../../src/serialterminal/src/PortClassification.cpp
    ../../../src/serialterminal/src/SerialByteDevice.cpp
    ../../../src/serialterminal/src/SerialByteDevice.h
    ../../../src/serialterminal/src/gui/serial/PortSelector.cpp
    ../../../src/serialterminal/src/gui/serial/PortSelector.h
)
set_target_properties(SerialDeviceTests PROPERTIES
    AUTORCC ON
)
target_compile_features(SerialDeviceTests PRIVATE cxx_std_20)
target_include_directories(SerialDeviceTests PRIVATE
    ../../../src/serialterminal/inc
    ../../../src/serialterminal/src
)
target_link_libraries(SerialDeviceTests PRIVATE
    Termaster::PluginApi
    Qt6::Test
    Qt6::Widgets
    Qt6::SerialPort
)
add_test(NAME SerialDeviceTests COMMAND SerialDeviceTests)
set_tests_properties(SerialDeviceTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(SerialPluginTests
    ../../../src/serialterminal/src/SerialTerminal.cpp
    SerialPluginTests.cpp
    ../../../src/serialterminal/src/SerialPlugin.cpp
    ../../../src/serialterminal/src/SerialPlugin.h
    ../../../src/serialterminal/src/PortClassification.cpp
    ../../../src/serialterminal/src/SerialByteDevice.cpp
    ../../../src/serialterminal/src/gui/serial/PortSelector.cpp
    ../../../src/serialterminal/resources/SerialTerminal.qrc
)
set_target_properties(SerialPluginTests PROPERTIES
    AUTORCC ON
)
target_compile_features(SerialPluginTests PRIVATE cxx_std_20)
target_include_directories(SerialPluginTests PRIVATE
    ../../../src/serialterminal/inc
    ../../../src/serialterminal/src
)
target_link_libraries(SerialPluginTests PRIVATE
    Termaster::PluginApi
    Qt6::Test
    Qt6::Widgets
    Qt6::SerialPort
)
add_test(NAME SerialPluginTests COMMAND SerialPluginTests)
set_tests_properties(SerialPluginTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(PortClassificationTests
    PortClassificationTests.cpp
    ../../../src/serialterminal/src/PortClassification.cpp
)
target_compile_features(PortClassificationTests PRIVATE cxx_std_20)
target_include_directories(PortClassificationTests PRIVATE
    ../../../src/serialterminal/src
)
target_link_libraries(PortClassificationTests PRIVATE
    Qt6::Test
)
add_test(NAME PortClassificationTests COMMAND PortClassificationTests)
if(WIN32)
    foreach(serial_test IN ITEMS
        SerialDeviceTests
        SerialPluginTests
        PortClassificationTests
    )
        target_link_libraries(${serial_test} PRIVATE
            setupapi
            cfgmgr32
            advapi32
        )
    endforeach()
endif()
