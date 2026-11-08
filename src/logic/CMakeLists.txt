project(Logic)

find_package(Qt6 REQUIRED COMPONENTS
    Core
)

qt_add_library(${PROJECT_NAME} STATIC
    inc/Termaster/Logic/GeneralLogic.h
    src/GeneralLogic.cpp

)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Core
    Managers
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
