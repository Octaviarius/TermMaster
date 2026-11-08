project(Core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    SerialBus
    SerialPort
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Core/Version.h
    src/Version.cpp
    inc/Termaster/Core/RandomNames.h
    src/RandomNames.cpp
    inc/Termaster/Core/Registree.h
    src/Registree.cpp
    inc/Termaster/Core/Path.h
    src/Path.cpp
    inc/Termaster/Core/Singleton.h
    inc/Termaster/Core/utils.h
    inc/Termaster/Core/Command.h
    src/Command.cpp
    inc/Termaster/Core/Settings.h
    src/Settings.cpp
    inc/Termaster/Core/gui/action/ActionContainerAdapter.h
    src/gui/action/ActionContainerAdapter.cpp
    inc/Termaster/Core/ActionContainer.h
    src/ActionContainer.cpp
    inc/Termaster/Core/gui/action/MenubarActionContainer.h
    src/gui/action/MenubarActionContainer.cpp
    inc/Termaster/Core/gui/action/MenuActionContainer.h
    src/gui/action/MenuActionContainer.cpp
    inc/Termaster/Core/gui/action/ToolbarActionContainer.h
    src/gui/action/ToolbarActionContainer.cpp
    inc/Termaster/Core/EnumReflection.h
    inc/Termaster/Core/Deque.h
    inc/Termaster/Core/plugin/Plugin.h
    inc/Termaster/Core/plugin/PluginManager.h
    src/plugin/PluginManager.cpp
    inc/Termaster/Core/gui/plugin/PluginManagerGui.h
    src/gui/plugin/PluginManagerGui.cpp
    src/gui/plugin/PluginManagerWindow.h
    src/gui/plugin/PluginManagerWindow.cpp
    src/gui/plugin/PluginManagerController.h
    src/gui/plugin/PluginManagerController.cpp
    src/gui/plugin/PluginTableModel.h
    src/gui/plugin/PluginTableModel.cpp

)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::SerialPort
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Managers
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
