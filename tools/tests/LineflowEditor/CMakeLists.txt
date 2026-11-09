find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)
qt_add_executable(LineflowEditorTests
    LineflowEditorTests.cpp
)
target_compile_features(LineflowEditorTests PRIVATE cxx_std_20)
target_include_directories(LineflowEditorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/App/src"
)
target_link_libraries(LineflowEditorTests PRIVATE
    App
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME LineflowEditorTests COMMAND LineflowEditorTests)
set_tests_properties(LineflowEditorTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
