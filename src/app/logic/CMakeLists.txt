project(Logic)

find_package(Qt6 REQUIRED COMPONENTS
    Core    
)

qt_add_library(${PROJECT_NAME} STATIC
    GeneralLogic.h GeneralLogic.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Core
    Managers
)
