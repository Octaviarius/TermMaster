find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(TerminalDisplayTests
    TerminalDisplayTests.cpp
)

target_link_libraries(TerminalDisplayTests PRIVATE
    Core
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME TerminalDisplayTests COMMAND TerminalDisplayTests)

set_tests_properties(TerminalDisplayTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
