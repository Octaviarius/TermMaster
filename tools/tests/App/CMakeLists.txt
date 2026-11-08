find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(AppTests
    AppTests.cpp
)
target_compile_features(AppTests PRIVATE cxx_std_20)
target_link_libraries(AppTests PRIVATE
    App
    Qt6::Test
)
add_test(NAME AppTests COMMAND AppTests)
set_tests_properties(AppTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
