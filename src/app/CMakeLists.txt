project(TermMaster)

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
    yaml-cpp
    CoreModule
    Utils
    ExtensionSystem
)
