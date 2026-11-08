project(Widgets)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Widgets/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui
    inc/Termaster/Widgets/AboutDialog.h
    src/AboutDialog.cpp
    src/AboutDialog.ui
    inc/Termaster/Widgets/SettingsDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.ui
    inc/Termaster/Widgets/IConfigWidget.h
    src/config/IConfigWidget.cpp
    inc/Termaster/Widgets/ConfigContainer.h
    src/config/ConfigContainer.cpp
    inc/Termaster/Widgets/ConfigSerialTerminalWidget.h
    src/config/ConfigSerialTerminalWidget.cpp
    src/config/ConfigSerialTerminalWidget.ui
    inc/Termaster/Widgets/TermWidget.h
    src/TermWidget/TermWidget.cpp
    inc/Termaster/Widgets/TermModel.h
    src/TermWidget/TermModel.cpp
    inc/Termaster/Widgets/TermChar.h
    src/TermWidget/TermChar.cpp
    inc/Termaster/Widgets/TermCursor.h
    src/TermWidget/TermCursor.cpp
    inc/Termaster/Widgets/TermColormap.h
    src/TermWidget/TermColormap.cpp

)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::SerialPort
    Core
    Managers
)


target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
