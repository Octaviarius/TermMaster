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
    configure_file(app/res/AppIcon.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc"
        @ONLY
    )
    target_sources(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc"
    )
endif()

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
    App
)


add_subdirectory(core)
add_subdirectory(app)
