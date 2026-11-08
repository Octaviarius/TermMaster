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

foreach(terminal_suite IN ITEMS
    VtParserTests
    TerminalScreenTests
    TerminalInputTests
    TerminalGraphicsTests
)
    qt_add_executable(${terminal_suite}
        ${terminal_suite}.cpp
    )
    target_link_libraries(${terminal_suite} PRIVATE
        Core
        Qt6::Test
    )
    add_test(NAME ${terminal_suite} COMMAND ${terminal_suite})
    set_tests_properties(${terminal_suite} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endforeach()

qt_add_executable(TerminalDemo
    TerminalDemo.cpp
)
target_link_libraries(TerminalDemo PRIVATE
    Core
    Qt6::Widgets
)
