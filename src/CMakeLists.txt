project(Termaster)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
)

qt_standard_project_setup()

configure_file(version.h.in
    "${CMAKE_BINARY_DIR}/generated/Termaster/Application/BuildVersion.h"
)

qt_add_executable(${PROJECT_NAME}
    main.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    set(TERMASTER_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../assets/IconOrange.ico")
    configure_file(App/Res/AppIcon.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc"
        @ONLY
    )
    target_sources(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc"
    )
endif()

qt_add_resources(${PROJECT_NAME} "icons"
    PREFIX "/icons"
    BASE "Res/Icon"
    FILES
    Res/Icon/cat-terminal.svg
    Res/Icon/cat-appearance.svg
    Res/Icon/cat-connections.svg
    Res/Icon/cat-dialogs.svg
    Res/Icon/cat-keyboard.svg
    Res/Icon/cat-mouse.svg
    Res/Icon/cat-shortcuts.svg
    Res/Icon/connected.svg
    Res/Icon/disconnected.svg
    Res/Icon/new-terminal.svg
    Res/Icon/new-terminal-console.svg
    Res/Icon/new-terminal-serial.svg
    Res/Icon/new-terminal-socket.svg
    Res/Icon/new-terminal-ssh.svg
    Res/Icon/device-usb.svg
    Res/Icon/device-pci.svg
    Res/Icon/device-virtual.svg
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    App
)


add_subdirectory(Core)
add_subdirectory(App)

add_subdirectory(SerialTerminal)

add_subdirectory(ConsoleTerminal)

add_subdirectory(SocketTerminal)
