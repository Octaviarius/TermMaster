project(App)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
)

qt_add_library(App STATIC
    inc/Termaster/App/App.h
    src/App.cpp
    src/gui/main/MainWindow.h
    src/gui/main/MainWindow.cpp
    src/gui/main/EditActions.h
    src/gui/main/EditActions.cpp
    src/gui/settings/SettingsDialog.h
    src/gui/settings/SettingsDialog.cpp
    src/gui/settings/SettingsPreview.h
    src/gui/settings/SettingsPreview.cpp
    src/gui/workspace/WorkspacePage.h
    src/gui/workspace/WorkspacePage.cpp
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
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/res"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/res/SettingsSchema.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings/ApplicationPreview.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings/WorkspacePreview.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings/SessionPreview.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings/TerminalPreview.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/settings/SerialPreview.yaml"
)
