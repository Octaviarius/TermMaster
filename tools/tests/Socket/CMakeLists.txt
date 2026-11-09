find_package(Qt6 REQUIRED COMPONENTS
    Network
    Test
    Widgets
)
foreach(socket_test IN ITEMS
    SocketDeviceTests
    SocketPluginTests
)
    qt_add_executable(${socket_test}
        ${socket_test}.cpp
        ../../../src/SocketTerminal/src/SocketDevice.cpp
    )
    target_compile_features(${socket_test} PRIVATE cxx_std_20)
    target_include_directories(${socket_test} PRIVATE
        ../../../src/SocketTerminal/src
        ../../../src/SocketTerminal/inc
    )
    target_link_libraries(${socket_test} PRIVATE
        Termaster::PluginApi
        Qt6::Network
        Qt6::Test
        Qt6::Widgets
    )
    add_test(NAME ${socket_test} COMMAND ${socket_test})
    set_tests_properties(${socket_test} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endforeach()
target_sources(SocketPluginTests PRIVATE
    ../../../src/SocketTerminal/src/SocketTerminal.cpp
    ../../../src/SocketTerminal/src/SocketPlugin.cpp
    ../../../src/SocketTerminal/src/SocketPlugin.h
    ../../../src/SocketTerminal/Resources/SocketTerminal.qrc
)
set_target_properties(SocketPluginTests PROPERTIES
    AUTORCC ON
)
qt_add_executable(SocketSettingsTests
    SocketSettingsTests.cpp
    ../../../src/SocketTerminal/Resources/SocketTerminal.qrc
)
set_target_properties(SocketSettingsTests PROPERTIES
    AUTORCC ON
)
target_compile_features(SocketSettingsTests PRIVATE cxx_std_20)
target_include_directories(SocketSettingsTests PRIVATE
    ../../../src/App/src
)
target_link_libraries(SocketSettingsTests PRIVATE
    App
    Qt6::Test
)
add_test(NAME SocketSettingsTests COMMAND SocketSettingsTests)
set_tests_properties(SocketSettingsTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
