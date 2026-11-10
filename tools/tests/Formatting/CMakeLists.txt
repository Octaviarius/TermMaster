find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(CompiledFormatTests
    CompiledFormatTests.cpp
)
target_link_libraries(CompiledFormatTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CompiledFormatTests COMMAND CompiledFormatTests)
