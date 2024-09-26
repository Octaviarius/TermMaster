project(Core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    SerialBus
    SerialPort
    Widgets
)

qt_add_library(${PROJECT_NAME} STATIC
    Version.h Version.cpp
    RandomNames.h RandomNames.cpp
    Registree.h Registree.cpp
    Path.h Path.cpp
    Singleton.h
    utils.h
    Command.h Command.cpp
    ISettings.h ISettings.cpp
    ActionContainer.h ActionContainer.cpp
    MenubarActionContainer.h MenubarActionContainer.cpp
    MenuActionContainer.h MenuActionContainer.cpp
    ToolbarActionContainer.h ToolbarActionContainer.cpp
    EnumReflection.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::SerialPort
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/inc)

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
file_to_qstringlist(${CMAKE_CURRENT_SOURCE_DIR}/nouns.lst nounsList nouns_var)
file_to_qstringlist(${CMAKE_CURRENT_SOURCE_DIR}/adjectives.lst adjectivesList adj_var)

file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/inc/core/randomNamesData.h CONTENT "#include <QStringList>\r\n${nouns_var}\r\n${adj_var}")

