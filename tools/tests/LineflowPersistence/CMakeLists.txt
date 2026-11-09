find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)
qt_add_executable(LineflowPersistenceTests
    LineflowPersistenceTests.cpp
    ../../../src/App/src/Processing/WorkspaceProcessingEdit.cpp
    ../../../src/App/src/Processing/WorkspaceProcessingEdit.h
)
target_include_directories(LineflowPersistenceTests PRIVATE
    ../../../src/App/src
)
target_link_libraries(LineflowPersistenceTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME LineflowPersistenceTests COMMAND LineflowPersistenceTests)
