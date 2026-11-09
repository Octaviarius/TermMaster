find_package(Qt6 REQUIRED COMPONENTS
    Test
    Widgets
)
qt_add_executable(ProcessingRoutingTests
    ProcessingRoutingTests.cpp
    ../../../src/App/src/Terminal/ProcessingRouter.h
    ../../../src/App/src/Terminal/ProcessingRouter.cpp
    ../../../src/App/src/Terminal/AdvancedSender.h
    ../../../src/App/src/Terminal/AdvancedSender.cpp
)
target_include_directories(ProcessingRoutingTests PRIVATE
    ../../../src/App/src/Terminal
)
target_link_libraries(ProcessingRoutingTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ProcessingRoutingTests COMMAND ProcessingRoutingTests)
