find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)
qt_add_executable(ProcessingTests
    ProcessingTests.cpp
)
target_link_libraries(ProcessingTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessingTests COMMAND ProcessingTests)

qt_add_executable(ProcessorDefaultsTests
    ProcessorDefaultsTests.cpp
)

target_link_libraries(ProcessorDefaultsTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessorDefaultsTests COMMAND ProcessorDefaultsTests)
