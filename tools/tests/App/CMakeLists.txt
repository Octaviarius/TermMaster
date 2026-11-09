find_package(Qt6 REQUIRED COMPONENTS
    Network
    Test
    Widgets
)

qt_add_executable(AppTests
    AppTests.cpp
)
target_compile_features(AppTests PRIVATE cxx_std_20)
target_link_libraries(AppTests PRIVATE
    App
    Qt6::Network
    Qt6::Test
)
add_test(NAME AppTests COMMAND AppTests)
set_tests_properties(AppTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

target_include_directories(AppTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/App/src"
)

add_dependencies(AppTests
    SerialTerminal
    ConsoleTerminal
    SocketTerminal
)
target_compile_definitions(AppTests PRIVATE
    TERMASTER_SOCKET_PLUGIN_FILE="$<TARGET_FILE:SocketTerminal>"
    TERMASTER_CONSOLE_PLUGIN_FILE="$<TARGET_FILE:ConsoleTerminal>"
    TERMASTER_SERIAL_PLUGIN_FILE="$<TARGET_FILE:SerialTerminal>"
)
