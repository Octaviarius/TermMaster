project(TermMaster)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_standard_project_setup()

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/inc/app/version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/inc)

qt_add_executable(${PROJECT_NAME}
    main.cpp
)

qt_add_resources(${PROJECT_NAME} "icons"
    PREFIX "/icons"
    BASE "res/icon"
    FILES
    res/icon/cat-terminal.svg
    res/icon/cat-appearance.svg
    res/icon/cat-connections.svg
    res/icon/cat-dialogs.svg
    res/icon/cat-keyboard.svg
    res/icon/cat-mouse.svg
    res/icon/cat-shortcuts.svg
    res/icon/connected.svg
    res/icon/disconnected.svg
    res/icon/new-terminal.svg
    res/icon/new-terminal-console.svg
    res/icon/new-terminal-serial.svg
    res/icon/new-terminal-socket.svg
    res/icon/new-terminal-ssh.svg
    res/icon/device-usb.svg
    res/icon/device-pci.svg
    res/icon/device-virtual.svg
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::SerialBus
    Core
    Managers
    Widgets
    Logic
    yaml-cpp
)

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(managers)
add_subdirectory(core)
add_subdirectory(widgets)
add_subdirectory(terminal)
add_subdirectory(logic)
