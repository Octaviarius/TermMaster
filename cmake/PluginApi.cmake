include(CMakePackageConfigHelpers)

set(plugin_api_root "${CMAKE_BINARY_DIR}/plugin/PluginApi")
set(plugin_api_package "${plugin_api_root}/lib/cmake/TermasterPluginApi")
foreach(api_header IN ITEMS
    plugin/Plugin.h
    plugin/HostedPlugin.h
    plugin/PluginHost.h
    terminal/SerialDevice.h
)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/../src/core/inc/Termaster/Core/${api_header}"
        "${plugin_api_root}/include/Termaster/Core/${api_header}"
        COPYONLY
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/TermasterPlugin.cmake"
    "${plugin_api_package}/TermasterPlugin.cmake"
    COPYONLY
)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/TermasterPluginApiConfig.cmake.in"
    "${plugin_api_package}/TermasterPluginApiConfig.cmake"
    INSTALL_DESTINATION lib/cmake/TermasterPluginApi
)

add_library(TermasterPluginApi INTERFACE)
add_library(Termaster::PluginApi ALIAS TermasterPluginApi)
target_compile_features(TermasterPluginApi INTERFACE cxx_std_20)
target_link_libraries(TermasterPluginApi INTERFACE
    Qt6::Core
    Qt6::Gui
)
target_include_directories(TermasterPluginApi INTERFACE "${plugin_api_root}/include")

include("${CMAKE_CURRENT_LIST_DIR}/TermasterPlugin.cmake")
