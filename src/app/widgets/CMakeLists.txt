project(Widgets)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_add_library(${PROJECT_NAME} STATIC
    MainWindow.h MainWindow.cpp MainWindow.ui
    AboutDialog.h AboutDialog.cpp AboutDialog.ui
    SettingsDialog.h SettingsDialog.cpp SettingsDialog.ui
    config/IConfigWidget.h config/IConfigWidget.cpp
    config/ConfigContainer.h
    config/ConfigContainer.cpp
    config/ConfigSerialTerminalWidget.h
    config/ConfigSerialTerminalWidget.cpp
    config/ConfigSerialTerminalWidget.ui
    TermWidget/TermWidget.h TermWidget/TermWidget.cpp
    TermWidget/TermModel.h TermWidget/TermModel.cpp
    TermWidget/TermChar.h TermWidget/TermChar.cpp
    TermWidget/TermCursor.h TermWidget/TermCursor.cpp
    TermWidget/TermColormap.h TermWidget/TermColormap.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::SerialPort
    Core
    Managers
)

