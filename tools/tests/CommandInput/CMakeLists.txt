find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(CommandInputTests
    CommandInputTests.cpp
)
target_link_libraries(CommandInputTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CommandInputTests COMMAND CommandInputTests)
