project(Core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Svg
)
set_property(TARGET Qt6::Svg PROPERTY IMPORTED_GLOBAL TRUE)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Core/Gui/Icons.h
    src/Gui/Icons.cpp
    inc/Termaster/Core/Gui/SelectionIconDelegate.h
    src/Gui/SelectionIconDelegate.cpp
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
    inc/Termaster/Core/CommandShortcuts.h
    src/CommandShortcuts.cpp
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
    inc/Termaster/Core/Processing/Processor.h
    inc/Termaster/Core/Processing/ProcessorCatalog.h
    inc/Termaster/Core/Processing/ProcessorRegistry.h
    src/Processing/ProcessorRegistry.cpp
    inc/Termaster/Core/Processing/ProcessorRefactoring.h
    src/Processing/ProcessorRefactoring.cpp
    inc/Termaster/Core/Processing/ProcessorBlocks.h
    src/Processing/ProcessorBlocks.cpp
    inc/Termaster/Core/Processing/Lineflow.h
    src/Processing/Lineflow.cpp
    inc/Termaster/Core/Processing/NativeProcessors.h
    src/Processing/NativeProcessors.cpp
    src/Processing/Nodes/Bypass.h
    src/Processing/Nodes/Bypass.cpp
    src/Processing/Nodes/Stub.h
    src/Processing/Nodes/Stub.cpp
    src/Processing/Nodes/Pacer.h
    src/Processing/Nodes/Pacer.cpp
    src/Processing/Nodes/Gate.h
    src/Processing/Nodes/Gate.cpp
    src/Processing/Nodes/Framer.h
    src/Processing/Nodes/Framer.cpp
    src/Processing/Nodes/TimedFramer.h
    src/Processing/Nodes/TimedFramer.cpp
    src/Processing/Nodes/Timestamp.h
    src/Processing/Nodes/Timestamp.cpp
    src/Processing/Nodes/PatternFramer.h
    src/Processing/Nodes/PatternFramer.cpp
    src/Processing/Nodes/HexEncoder.h
    src/Processing/Nodes/HexEncoder.cpp
    src/Processing/Nodes/HexDecoder.h
    src/Processing/Nodes/HexDecoder.cpp
    src/Processing/Nodes/Base64Encoder.h
    src/Processing/Nodes/Base64Encoder.cpp
    src/Processing/Nodes/Base64Decoder.h
    src/Processing/Nodes/Base64Decoder.cpp
    src/Processing/Nodes/SyncProcessorNode.h
    src/Processing/Nodes/DuplexCodecNode.h
    src/Processing/Nodes/DuplexCodecNode.cpp
    src/Processing/Nodes/HexCodec.h
    src/Processing/Nodes/HexCodec.cpp
    src/Processing/Nodes/Base64Codec.h
    src/Processing/Nodes/Base64Codec.cpp
    inc/Termaster/Core/WorkspaceFileTransaction.h
    src/WorkspaceFileTransaction.cpp
    inc/Termaster/Core/Workspace.h
    src/Workspace.cpp
    inc/Termaster/Core/Session.h
    src/Session.cpp
    inc/Termaster/Core/Plugin/Plugin.h
    inc/Termaster/Core/Plugin/HostedPlugin.h
    inc/Termaster/Core/Plugin/PluginHost.h
    inc/Termaster/Core/Terminal/SerialDevice.h
    inc/Termaster/Core/Terminal/TerminalSendQueue.h
    src/Terminal/TerminalSendQueue.cpp
    inc/Termaster/Core/Terminal/CommandInput.h
    src/Terminal/CommandInput.cpp
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
    Qt6::Svg
    yaml-cpp
    valijson
    pcre2-8-static
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PCRE2_STATIC
    PCRE2_CODE_UNIT_WIDTH=8
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

qt_add_resources(Core "actionIcons"
    PREFIX "/Termaster/Core/Icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/plus.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/trash.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/arrow-right.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/chevron-up.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/chevron-down.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/workflow.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/blocks.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/link.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/triangle-exclamation-point.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/app-window.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/keyboard.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/panels-top-left.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/text-cursor.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/terminal.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/cable.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/clipboard-paste.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/copy.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/eraser.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/file-plus.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/folder-open.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/folder-plus.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/network.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/panel-bottom.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/panel-top.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/pencil.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/play.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/plug.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/puzzle.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/redo-2.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/refresh-cw.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/rotate-cw.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/save.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/scan-text.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/scissors.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/sliders-horizontal.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/square-terminal.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/square.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/undo-2.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/unplug.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../components/lucide/icons/x.svg"
)

qt_add_resources(Core "processorIcons"
    PREFIX "/Termaster/Core/Icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/Res/Icons"
    FILES
        "Res/Icons/ProcessorElementary2.svg"
        "Res/Icons/ProcessorElementary4.svg"
        "Res/Icons/ProcessorComposite2.svg"
        "Res/Icons/ProcessorComposite4.svg"
        "Res/Icons/Processing.svg"
        "Res/Icons/Lineflow.svg"
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
