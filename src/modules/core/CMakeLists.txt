project(CoreModule)
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

qt_add_plugin(${PROJECT_NAME} 
	MODULE
	CLASS_NAME CoreModule
    CoreModule.h CoreModule.cpp CoreModule.json
    WindowManager.h WindowManager.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets yaml-cpp Utils ExtensionSystem)
