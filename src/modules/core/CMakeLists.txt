project(CoreModule)
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

qt_add_plugin(${PROJECT_NAME} SHARED
    CoreModule.h CoreModule.cpp CoreModule.json
    SettingsManager.h SettingsManager.cpp
    WindowManager.h WindowManager.cpp
    CommandManager.h CommandManager.cpp
    widgets/MainWindow.h widgets/MainWindow.cpp widgets/MainWindow.ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets yaml-cpp Utils ExtensionSystem)
