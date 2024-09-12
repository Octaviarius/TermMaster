project(Managers)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    CommandManager.h CommandManager.cpp
    ConfigManager.h ConfigManager.cpp
    WindowManager.h WindowManager.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Core
    Widgets
    yaml-cpp
)
