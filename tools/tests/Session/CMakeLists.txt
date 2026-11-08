find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(SessionTests
    SessionTests.cpp
)
target_compile_features(SessionTests PRIVATE cxx_std_20)
target_link_libraries(SessionTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME SessionTests COMMAND SessionTests)
set_tests_properties(SessionTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
