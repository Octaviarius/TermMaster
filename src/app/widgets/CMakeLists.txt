project(Widgets)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    MainWindow.h MainWindow.cpp MainWindow.ui
    AboutDialog.h AboutDialog.cpp AboutDialog.ui
    SettingsDialog.h SettingsDialog.cpp SettingsDialog.ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Core
    Managers
)
