project(Core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
)

qt_add_library(${PROJECT_NAME} ${LIBRARIES_TYPE}
    SettingsManager.h SettingsManager.cpp
    CommandManager.h CommandManager.cpp
    WindowManager.h WindowManager.cpp
    RandomNames.h RandomNames.cpp
    Builders.h Builders.cpp

    widgets/MainWindow.h widgets/MainWindow.cpp widgets/MainWindow.ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    yaml-cpp
    Utils
)

add_custom_command(
    TARGET ${PROJECT_NAME}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/nouns.lst ${CMAKE_BINARY_DIR}
)

add_custom_command(
    TARGET ${PROJECT_NAME}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/adjectives.lst ${CMAKE_BINARY_DIR}
)
