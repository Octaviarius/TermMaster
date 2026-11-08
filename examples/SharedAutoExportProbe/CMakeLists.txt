cmake_minimum_required(VERSION 3.21)

project(SharedAutoExportProbe LANGUAGES CXX)

find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core
)
qt_standard_project_setup()

if(NOT WIN32 OR NOT MSVC OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Use Windows/MSVC with single-config Ninja for this experiment")
endif()

option(PROBE_AUTO_EXPORT "Enable CMake automatic symbol export" ON)
set(probe_common "${CMAKE_CURRENT_SOURCE_DIR}/../PluginDependencyProbe")

add_library(Provider SHARED
    ProviderApi.h
    ProviderPlugin.h
    ProviderPlugin.cpp
)
add_library(Consumer SHARED
    ConsumerPlugin.h
    ConsumerPlugin.cpp
)

foreach(probe_target IN ITEMS
    Provider
    Consumer
)
    target_compile_features(${probe_target} PRIVATE cxx_std_20)
    target_link_libraries(${probe_target} PUBLIC Qt6::Core)
    set_target_properties(${probe_target} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS "${PROBE_AUTO_EXPORT}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/imports"
    )
endforeach()

target_include_directories(Provider PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${probe_common}>"
    "$<INSTALL_INTERFACE:include/SharedAutoExportProbe>"
)
target_link_libraries(Consumer PRIVATE Provider)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ProbeTargets.json"
    CONTENT [=[{
    "provider_type": "$<TARGET_PROPERTY:Provider,TYPE>",
    "consumer_type": "$<TARGET_PROPERTY:Consumer,TYPE>",
    "auto_export": $<IF:$<BOOL:$<TARGET_PROPERTY:Provider,WINDOWS_EXPORT_ALL_SYMBOLS>>,true,false>
}
]=]
)

qt_add_executable(PluginDependencyProbe
    "${probe_common}/Main.cpp"
)
target_compile_features(PluginDependencyProbe PRIVATE cxx_std_20)
target_link_libraries(PluginDependencyProbe PRIVATE Qt6::Core)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    SharedAutoExportProbeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SharedAutoExportProbeConfig.cmake"
    INSTALL_DESTINATION lib/cmake/SharedAutoExportProbe
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SharedAutoExportProbeConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)
install(TARGETS Provider
    EXPORT SharedAutoExportProbeTargets
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES
    ProviderApi.h
    "${probe_common}/ProbeInterface.h"
    DESTINATION include/SharedAutoExportProbe
)
install(EXPORT SharedAutoExportProbeTargets
    NAMESPACE SharedAutoExportProbe::
    DESTINATION lib/cmake/SharedAutoExportProbe
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SharedAutoExportProbeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SharedAutoExportProbeConfigVersion.cmake"
    DESTINATION lib/cmake/SharedAutoExportProbe
)
