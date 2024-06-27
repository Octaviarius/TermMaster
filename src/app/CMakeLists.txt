project(TermMaster)

add_subdirectory(utils)
add_subdirectory(widgets)
add_subdirectory(core)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_standard_project_setup()

qt_add_executable(${PROJECT_NAME}
    version.h
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::SerialBus
    Utils
    Widgets
    Core
    yaml-cpp
)

