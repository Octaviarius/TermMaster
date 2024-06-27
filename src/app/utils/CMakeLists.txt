project(Utils)

find_package(Qt6 REQUIRED COMPONENTS
    Core
)

qt_add_library(${PROJECT_NAME} ${LIBRARIES_TYPE}
    utils.h
    Singleton.h
    CompileTime.h
    Path.h Path.cpp
    Registree.h Registree.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    yaml-cpp
)
