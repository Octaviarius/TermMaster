find_package(Qt6 REQUIRED COMPONENTS
    Test
)

qt_add_executable(ThreadedSerialDeviceTests
    ThreadedSerialDeviceTests.cpp
)
target_link_libraries(ThreadedSerialDeviceTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME ThreadedSerialDeviceTests COMMAND ThreadedSerialDeviceTests -o -,txt)
