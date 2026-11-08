find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(WorkspaceTests
    WorkspaceTests.cpp
)
target_compile_features(WorkspaceTests PRIVATE cxx_std_20)
target_link_libraries(WorkspaceTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME WorkspaceTests COMMAND WorkspaceTests)
set_tests_properties(WorkspaceTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)
