project(Widgets)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
)

qt_add_library(${PROJECT_NAME} ${LIBRARIES_TYPE}

)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Utils
    Core
)
