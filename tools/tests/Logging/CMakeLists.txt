find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(LoggerTests
    LoggerTests.cpp
)
target_link_libraries(LoggerTests PRIVATE
    Core
)
add_test(NAME LoggerTests COMMAND LoggerTests)
set_tests_properties(LoggerTests PROPERTIES
    TIMEOUT 90
)

qt_add_executable(LoggingTests
    LoggingTests.cpp
)
target_link_libraries(LoggingTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME LoggingTests COMMAND LoggingTests)
set_tests_properties(LoggingTests PROPERTIES
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 60
)
