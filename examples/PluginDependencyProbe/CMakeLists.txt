cmake_minimum_required(VERSION 3.21)

project(PluginDependencyProbe LANGUAGES CXX)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
)

qt_standard_project_setup()

if(NOT WIN32 OR NOT (MINGW OR MSVC))
    message(FATAL_ERROR "This experiment targets Windows with MinGW or MSVC")
endif()

if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Use single-config Ninja and a separate build directory per configuration")
endif()

option(PROBE_DIRECT_TARGET_LINK "Demonstrate CMake rejecting a MODULE target dependency" OFF)
option(PROBE_EXPORT_API "Export the ordinary provider C++ API" ON)

set(probe_plugin_dir "${CMAKE_CURRENT_BINARY_DIR}/plugins")
if(MSVC)
    set(probe_import_library "${CMAKE_CURRENT_BINARY_DIR}/imports/Provider.lib")
else()
    set(probe_import_library "${CMAKE_CURRENT_BINARY_DIR}/imports/libProvider.dll.a")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/imports")

qt_add_plugin(Provider
    CLASS_NAME ProviderPlugin
    ProviderPlugin.h
    ProviderPlugin.cpp
    ProviderApi.h
    ProbeInterface.h
)

qt_add_plugin(Consumer
    CLASS_NAME ConsumerPlugin
    ConsumerPlugin.h
    ConsumerPlugin.cpp
    ProviderApi.h
    ProbeInterface.h
)

foreach(probe_target IN ITEMS
    Provider
    Consumer
)
    target_compile_features(${probe_target} PRIVATE cxx_std_20)
    target_link_libraries(${probe_target} PRIVATE Qt6::Core)
    set_target_properties(${probe_target} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${probe_plugin_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${probe_plugin_dir}"
    )
endforeach()

target_compile_definitions(Provider PRIVATE PROVIDER_BUILD)
if(NOT PROBE_EXPORT_API)
    target_compile_definitions(Provider PRIVATE PROVIDER_NO_API_EXPORT)
endif()

if(PROBE_DIRECT_TARGET_LINK)
    target_link_libraries(Consumer PRIVATE Provider)
else()
    # Qt creates MODULE targets. Make the DLL's explicit exports available to
    # the consumer through an import library rather than linking a MODULE target.
    if(MSVC)
        target_link_options(Provider PRIVATE "LINKER:/IMPLIB:${probe_import_library}")
    else()
        target_link_options(Provider PRIVATE "LINKER:--out-implib,${probe_import_library}")
    endif()
    add_custom_command(TARGET Provider POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E touch "${probe_import_library}"
        BYPRODUCTS "${probe_import_library}"
        VERBATIM
    )
    add_dependencies(Consumer Provider)
    target_link_libraries(Consumer PRIVATE "${probe_import_library}")
endif()

qt_add_executable(PluginDependencyProbe
    Main.cpp
    ProbeInterface.h
)

target_compile_features(PluginDependencyProbe PRIVATE cxx_std_20)
target_link_libraries(PluginDependencyProbe PRIVATE Qt6::Core)

include(CMakePackageConfigHelpers)

get_filename_component(probe_import_name "${probe_import_library}" NAME)
configure_package_config_file(
    PluginDependencyProbeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/PluginDependencyProbeConfig.cmake"
    INSTALL_DESTINATION lib/cmake/PluginDependencyProbe
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PluginDependencyProbeConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

install(TARGETS Provider
    LIBRARY DESTINATION bin
    RUNTIME DESTINATION bin
)
install(FILES
    ProviderApi.h
    ProbeInterface.h
    DESTINATION include/PluginDependencyProbe
)
install(FILES "${probe_import_library}"
    DESTINATION lib
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PluginDependencyProbeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PluginDependencyProbeConfigVersion.cmake"
    DESTINATION lib/cmake/PluginDependencyProbe
)
