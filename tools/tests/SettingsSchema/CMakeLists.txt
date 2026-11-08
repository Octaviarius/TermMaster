find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(SettingsSchemaTests
    SettingsSchemaTests.cpp
)
target_compile_features(SettingsSchemaTests PRIVATE cxx_std_20)
target_link_libraries(SettingsSchemaTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME SettingsSchemaTests COMMAND SettingsSchemaTests)
set_tests_properties(SettingsSchemaTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
