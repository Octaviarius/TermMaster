find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(SettingsTests
    SettingsTests.cpp
)
target_compile_features(SettingsTests PRIVATE cxx_std_20)
target_link_libraries(SettingsTests PRIVATE
    Core
    Qt6::Test
    yaml-cpp
)
add_test(NAME SettingsTests COMMAND SettingsTests)
set_tests_properties(SettingsTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
