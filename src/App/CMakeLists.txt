project(App)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
)

qt_add_library(App STATIC
    inc/Termaster/App/App.h
    src/App.cpp
    src/Terminal/AdvancedSender.h
    src/Terminal/AdvancedSender.cpp
    src/Gui/Main/MainWindow.h
    src/Gui/Main/MainWindow.cpp
    src/Gui/Main/EditActions.h
    src/Gui/Main/EditActions.cpp
    src/Gui/Settings/SettingsDialog.h
    src/Gui/Settings/SettingsDialog.cpp
    src/Gui/Settings/FavoritesEditor.h
    src/Gui/Settings/FavoritesEditor.cpp
    src/Gui/Settings/ShortcutEditor.h
    src/Gui/Settings/ShortcutEditor.cpp
    src/Gui/Session/SessionPicker.h
    src/Gui/Session/SessionPicker.cpp
    src/Gui/Terminal/CommandEditor.h
    src/Gui/Terminal/CommandEditor.cpp
    src/Gui/Terminal/TerminalController.h
    src/Gui/Terminal/TerminalController.cpp
    src/Gui/Workspace/WorkspacePage.h
    src/Gui/Workspace/WorkspacePage.cpp
)

target_compile_features(App PUBLIC cxx_std_20)

target_link_libraries(App PUBLIC
    Qt6::Widgets
    Core
)

target_include_directories(App PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

qt_add_resources(App "appIcons"
    PREFIX "/Termaster/App"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/IconOrange.png"
)

qt_add_resources(App "appSettingsSchema"
    PREFIX "/Termaster/App"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/Res"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/SettingsSchema.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Shortcuts.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/TerminalAppearance.yaml"
)

qt_add_resources(App "connectionStatusIcons"
    PREFIX "/Termaster/App/Status"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status/Closed.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status/Open.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status/Busy.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status/Failed.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Status/Retry.svg"
)
