project(Utils)
find_package(Qt6 COMPONENTS Core REQUIRED)

qt_add_library(${PROJECT_NAME} SHARED
    utils.h
    Path.h
    Registree.h
    Path.cpp
    Registree.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
