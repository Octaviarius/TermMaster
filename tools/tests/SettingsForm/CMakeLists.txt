find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)
qt_add_executable(SettingsFormTests
    SettingsFormTests.cpp
)
target_compile_features(SettingsFormTests PRIVATE cxx_std_20)
target_link_libraries(SettingsFormTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME SettingsFormTests COMMAND SettingsFormTests)
set_tests_properties(SettingsFormTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
