find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
    SerialBus
    SerialPort
)

qt_add_executable(ActionContainerTests
    ActionContainerTests.cpp
)
target_compile_features(ActionContainerTests PRIVATE cxx_std_20)
target_link_libraries(ActionContainerTests PRIVATE
    Core
    Managers
    Qt6::Test
)
add_test(NAME ActionContainerTests COMMAND ActionContainerTests)
set_tests_properties(ActionContainerTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
