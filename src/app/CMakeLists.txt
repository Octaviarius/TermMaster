project(TermMaster)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    SerialPort
)

qt_standard_project_setup()

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/inc/app/version.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/inc)

qt_add_executable(${PROJECT_NAME}
    main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::SerialBus
    Core
    Managers
    Widgets
    Logic
    yaml-cpp
)

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(managers)
add_subdirectory(core)
add_subdirectory(widgets)
add_subdirectory(logic)
