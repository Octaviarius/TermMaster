if(NOT WIN32)
    message(FATAL_ERROR "ApplicationProfile currently requires Windows process counters")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    SerialPort
    Widgets
)
qt_add_executable(ApplicationProfile
    ApplicationProfile.cpp
)
target_link_libraries(ApplicationProfile PRIVATE
    App
    Qt6::SerialPort
    psapi
)
target_compile_features(ApplicationProfile PRIVATE cxx_std_20)

if(MSVC)
    target_link_options(ApplicationProfile PRIVATE
        /MAP
    )
endif()
