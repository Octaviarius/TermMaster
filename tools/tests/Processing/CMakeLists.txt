find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)

qt_add_executable(CodeflowTests
    CodeflowTests.cpp
)
target_compile_features(CodeflowTests PRIVATE cxx_std_20)
target_link_libraries(CodeflowTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CodeflowTests COMMAND CodeflowTests -o -,txt)
set_tests_properties(CodeflowTests PROPERTIES
    TIMEOUT 90
)

qt_add_executable(ThreadedProcessorTests
    ThreadedProcessorTests.cpp
)
target_compile_features(ThreadedProcessorTests PRIVATE cxx_std_20)
target_link_libraries(ThreadedProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ThreadedProcessorTests COMMAND ThreadedProcessorTests -o -,txt)

qt_add_executable(DuplexProcessorTests
    DuplexProcessorTests.cpp
)
target_link_libraries(DuplexProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME DuplexProcessorTests COMMAND DuplexProcessorTests)

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

qt_add_executable(TimedFramerProcessorTests
    TimedFramerProcessorTests.cpp
)

target_include_directories(TimedFramerProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)

target_link_libraries(TimedFramerProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME TimedFramerProcessorTests COMMAND TimedFramerProcessorTests)

qt_add_executable(PatternFramerProcessorTests
    PatternFramerProcessorTests.cpp
)

target_include_directories(PatternFramerProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)

target_link_libraries(PatternFramerProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME PatternFramerProcessorTests COMMAND PatternFramerProcessorTests)

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

qt_add_executable(CompressionProcessorTests
    CompressionProcessorTests.cpp
)
target_include_directories(CompressionProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(CompressionProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME CompressionProcessorTests COMMAND CompressionProcessorTests)

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

qt_add_executable(TimestampProcessorTests
    TimestampProcessorTests.cpp
)
target_include_directories(TimestampProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(TimestampProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME TimestampProcessorTests COMMAND TimestampProcessorTests)

qt_add_executable(ProcessorPropertiesTests
    ProcessorPropertiesTests.cpp
)
target_link_libraries(ProcessorPropertiesTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessorPropertiesTests COMMAND ProcessorPropertiesTests)

qt_add_executable(PacerProcessorTests
    PacerProcessorTests.cpp
)
target_link_libraries(PacerProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME PacerProcessorTests COMMAND PacerProcessorTests)

qt_add_executable(LengthProcessorTests
    LengthProcessorTests.cpp
)

target_include_directories(LengthProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)

target_link_libraries(LengthProcessorTests PRIVATE
    Core
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME LengthProcessorTests COMMAND LengthProcessorTests)

qt_add_executable(ChecksumProcessorTests
    ChecksumProcessorTests.cpp
)

target_include_directories(ChecksumProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)

target_link_libraries(ChecksumProcessorTests PRIVATE
    Core
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME ChecksumProcessorTests COMMAND ChecksumProcessorTests)

qt_add_executable(StuffingProcessorTests
    StuffingProcessorTests.cpp
)

target_include_directories(StuffingProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)

target_link_libraries(StuffingProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME StuffingProcessorTests COMMAND StuffingProcessorTests)
set_tests_properties(StuffingProcessorTests PROPERTIES
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(ProcessorFamilyTests
    ProcessorFamilyTests.cpp
)
target_include_directories(ProcessorFamilyTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(ProcessorFamilyTests PRIVATE
    Core
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME ProcessorFamilyTests COMMAND ProcessorFamilyTests)

qt_add_executable(LineflowEndpointTests
    LineflowEndpointTests.cpp
)
target_link_libraries(LineflowEndpointTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME LineflowEndpointTests COMMAND LineflowEndpointTests)

qt_add_executable(TapProcessorTests
    TapProcessorTests.cpp
)
target_include_directories(TapProcessorTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Core/src/Processing"
)
target_link_libraries(TapProcessorTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME TapProcessorTests COMMAND TapProcessorTests)

if(Python3_Interpreter_FOUND)
    add_test(NAME PythonSdkTests
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/tests/Python/SdkTests.py"
    )
    set_tests_properties(PythonSdkTests PROPERTIES TIMEOUT 90)
endif()
add_dependencies(CodeflowTests PythonRuntime)
