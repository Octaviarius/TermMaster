project(Core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Core/ApplicationPaths.h
    src/ApplicationPaths.cpp
    inc/Termaster/Core/Version.h
    inc/Termaster/Core/BuildTime.h
    inc/Termaster/Core/CompileTime.h
    src/Version.cpp
    inc/Termaster/Core/RandomNames.h
    src/RandomNames.cpp
    inc/Termaster/Core/PurePath.h
    src/PurePath.cpp
    inc/Termaster/Core/Path.h
    src/Path.cpp
    inc/Termaster/Core/CommandRegistry.h
    src/CommandRegistry.cpp
    inc/Termaster/Core/Command.h
    src/Command.cpp
    inc/Termaster/Core/Gui/Action/ActionContainerAdapter.h
    src/Gui/Action/ActionContainerAdapter.cpp
    inc/Termaster/Core/ActionContainer.h
    src/ActionContainer.cpp
    inc/Termaster/Core/Gui/Action/MenubarActionContainer.h
    src/Gui/Action/MenubarActionContainer.cpp
    inc/Termaster/Core/Gui/Action/MenuActionContainer.h
    src/Gui/Action/MenuActionContainer.cpp
    inc/Termaster/Core/Gui/Action/ToolbarActionContainer.h
    src/Gui/Action/ToolbarActionContainer.cpp
    inc/Termaster/Core/Settings.h
    src/Settings.cpp
    inc/Termaster/Core/SettingsSchema.h
    src/SettingsSchema.cpp
    inc/Termaster/Core/Gui/Settings/SettingsFieldEditor.h
    src/Gui/Settings/SettingsFieldEditor.cpp
    inc/Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h
    src/Gui/Settings/SettingsWidgetRegistry.cpp
    inc/Termaster/Core/Gui/Settings/SettingsFormModel.h
    src/Gui/Settings/SettingsFormModel.cpp
    inc/Termaster/Core/Gui/Settings/SettingsFormWidget.h
    src/Gui/Settings/SettingsFormWidget.cpp
    inc/Termaster/Core/SettingsOverlay.h
    src/SettingsOverlay.cpp
    inc/Termaster/Core/Workspace.h
    src/Workspace.cpp
    inc/Termaster/Core/Session.h
    src/Session.cpp
    inc/Termaster/Core/Plugin/Plugin.h
    inc/Termaster/Core/Plugin/HostedPlugin.h
    inc/Termaster/Core/Plugin/PluginHost.h
    inc/Termaster/Core/Terminal/SerialDevice.h
    inc/Termaster/Core/Terminal/VtParser.h
    src/Terminal/VtParser.cpp
    inc/Termaster/Core/Terminal/TerminalInput.h
    src/Terminal/TerminalInput.cpp
    inc/Termaster/Core/Terminal/TerminalGraphics.h
    src/Terminal/TerminalGraphics.cpp
    src/Terminal/TerminalUnicode.h
    inc/Termaster/Core/Terminal/TerminalScreen.h
    src/Terminal/TerminalScreen.cpp
    inc/Termaster/Core/Gui/Terminal/TerminalDisplay.h
    src/Gui/Terminal/TerminalDisplay.cpp
    inc/Termaster/Core/Plugin/PluginManager.h
    src/Plugin/PluginManager.cpp
    inc/Termaster/Core/Gui/Plugin/PluginManagerGui.h
    src/Gui/Plugin/PluginManagerGui.cpp
    src/Gui/Plugin/PluginManagerWindow.h
    src/Gui/Plugin/PluginManagerWindow.cpp
    src/Gui/Plugin/PluginManagerController.h
    src/Gui/Plugin/PluginManagerController.cpp
    src/Gui/Plugin/PluginTableModel.h
    src/Gui/Plugin/PluginTableModel.cpp

)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt6::Core
    Qt6::Widgets
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    yaml-cpp
    valijson
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)

function(file_to_qstringlist file var_name out_var)
    file(STRINGS ${file} lines)
    set(out "const QStringList ${var_name} {")
    foreach(line ${lines})
        list(APPEND out "\"${line}\",")
    endforeach()
    string(JOIN "\r\n" out ${out})

    set(${out_var} "${out}\r\n};" PARENT_SCOPE)
endfunction()

# create a variables file
file_to_qstringlist(${CMAKE_CURRENT_SOURCE_DIR}/src/nouns.lst nounsList nouns_var)
file_to_qstringlist(${CMAKE_CURRENT_SOURCE_DIR}/src/adjectives.lst adjectivesList adj_var)

file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/RandomNamesData.h CONTENT "#include <QStringList>\r\n${nouns_var}\r\n${adj_var}")


target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set(settings_meta_schema "${CMAKE_CURRENT_SOURCE_DIR}/../../components/valijson/doc/schema/draft-07.json")
set_source_files_properties("${settings_meta_schema}" PROPERTIES
    QT_RESOURCE_ALIAS "SettingsSchemaDraft7.json"
)
qt_add_resources(Core "settingsSchemaMeta"
    PREFIX "/Termaster/Core"
    FILES
        "${settings_meta_schema}"
)

qt_add_resources(Core "settingsFormIcons"
    PREFIX "/Termaster/Core/Gui/Settings"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/Res/Gui/Settings"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Res/Gui/Settings/ResetOverride.svg"
)

# Use the platform zlib when available; the selected Qt kit also provides its bundled headers.
find_package(ZLIB QUIET)
if(TARGET ZLIB::ZLIB)
    target_link_libraries(Core PRIVATE
        ZLIB::ZLIB
    )
else()
    find_package(Qt6 REQUIRED COMPONENTS
        ZlibPrivate
    )
    set_property(TARGET Qt6::ZlibPrivate PROPERTY IMPORTED_GLOBAL TRUE)
    target_link_libraries(Core PRIVATE
        Qt6::ZlibPrivate
    )
endif()
