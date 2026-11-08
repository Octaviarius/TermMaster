find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
    SerialBus
    SerialPort
)

qt_add_executable(PathTests
    PathTests.cpp
)
target_compile_features(PathTests PRIVATE cxx_std_20)
target_link_libraries(PathTests PRIVATE
    Core
    Managers
    Qt6::Test
)
add_test(NAME PathTests COMMAND PathTests)
set_tests_properties(PathTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
