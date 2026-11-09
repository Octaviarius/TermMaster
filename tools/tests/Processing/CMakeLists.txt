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

qt_add_executable(ProcessorCatalogTests
    ProcessorCatalogTests.cpp
)
target_link_libraries(ProcessorCatalogTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessorCatalogTests COMMAND ProcessorCatalogTests)

qt_add_executable(ProcessorRefactoringTests
    ProcessorRefactoringTests.cpp
)

target_link_libraries(ProcessorRefactoringTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessorRefactoringTests COMMAND ProcessorRefactoringTests)

qt_add_executable(FramerProcessorTests
    FramerProcessorTests.cpp
)

target_include_directories(FramerProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(FramerProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME FramerProcessorTests COMMAND FramerProcessorTests)

qt_add_executable(CodecProcessorTests
    CodecProcessorTests.cpp
)

target_include_directories(CodecProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(CodecProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CodecProcessorTests COMMAND CodecProcessorTests)

qt_add_executable(GateProcessorTests
    GateProcessorTests.cpp
)

target_include_directories(GateProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(GateProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME GateProcessorTests COMMAND GateProcessorTests)

qt_add_executable(ProcessorPropertiesTests
    ProcessorPropertiesTests.cpp
)
target_link_libraries(ProcessorPropertiesTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessorPropertiesTests COMMAND ProcessorPropertiesTests)
