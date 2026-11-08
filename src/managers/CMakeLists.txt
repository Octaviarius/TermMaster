project(Managers)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    SerialBus
    SerialPort
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Managers/CommandManager.h
    src/CommandManager.cpp
    inc/Termaster/Managers/SettingsManager.h
    src/SettingsManager.cpp
    inc/Termaster/Managers/WindowManager.h
    src/WindowManager.cpp
    inc/Termaster/Managers/TerminalManager.h
    src/TerminalManager.cpp

)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::SerialBus
    Qt::SerialPort
    Core
    Widgets
    Terminal
    yaml-cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
