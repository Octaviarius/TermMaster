find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(CommandTests
    CommandTests.cpp
)
target_compile_features(CommandTests PRIVATE cxx_std_20)
target_link_libraries(CommandTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CommandTests COMMAND CommandTests)
set_tests_properties(CommandTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
