project(Widgets)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Widgets/gui/window/MainWindow.h
    src/gui/window/MainWindow.cpp
    src/gui/window/MainWindow.ui
    inc/Termaster/Widgets/gui/about/AboutDialog.h
    src/gui/about/AboutDialog.cpp
    src/gui/about/AboutDialog.ui
    inc/Termaster/Widgets/gui/settings/SettingsDialog.h
    src/gui/settings/SettingsDialog.cpp
    src/gui/settings/SettingsDialog.ui
    inc/Termaster/Widgets/gui/settings/IConfigWidget.h
    src/gui/settings/IConfigWidget.cpp
    inc/Termaster/Widgets/gui/settings/ConfigContainer.h
    src/gui/settings/ConfigContainer.cpp
    inc/Termaster/Widgets/gui/settings/ConfigSerialTerminalWidget.h
    src/gui/settings/ConfigSerialTerminalWidget.cpp
    src/gui/settings/ConfigSerialTerminalWidget.ui
    inc/Termaster/Widgets/gui/terminal/TermWidget.h
    src/gui/terminal/TermWidget.cpp
    inc/Termaster/Widgets/TermModel.h
    src/TermModel.cpp
    inc/Termaster/Widgets/TermChar.h
    src/TermChar.cpp
    inc/Termaster/Widgets/TermCursor.h
    src/TermCursor.cpp
    inc/Termaster/Widgets/gui/terminal/TermColormap.h
    src/gui/terminal/TermColormap.cpp

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
