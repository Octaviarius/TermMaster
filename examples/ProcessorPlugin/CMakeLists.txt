cmake_minimum_required(VERSION 3.21)
project(TermasterProcessorTutorial LANGUAGES CXX)

if(NOT TARGET Termaster::PluginApi)
    find_package(TermasterPluginApi CONFIG REQUIRED)
endif()

termaster_add_plugin(ProcessorTutorialPlugin
    OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/tutorial-plugin"
    SOURCES
        src/PrefixProcessor.cpp
        src/PrefixProcessor.h
        src/PrefixPlugin.cpp
        src/PrefixPlugin.h
        src/PrefixPlugin.json
        Resources/ProcessorTutorial.qrc
)
set_target_properties(ProcessorTutorialPlugin PROPERTIES
    AUTORCC ON
)
# Compiled help packaging is supplied by termaster_compile_help (see cmake/Help.cmake).


# The loadable plugin above needs only the public SDK and Qt. The optional
# integration executable uses the application's real host implementation.
if(TARGET Core)
    find_package(Qt6 REQUIRED COMPONENTS
        Test
    )
    qt_add_executable(ProcessorTutorialTests
        tests/ProcessorTutorialTests.cpp
    )
    target_compile_features(ProcessorTutorialTests PRIVATE cxx_std_20)
    target_link_libraries(ProcessorTutorialTests PRIVATE
        Core
        Qt6::Test
    )
    target_compile_definitions(ProcessorTutorialTests PRIVATE
        "TUTORIAL_PLUGIN=\"$<TARGET_FILE:ProcessorTutorialPlugin>\""
    )
    add_dependencies(ProcessorTutorialTests ProcessorTutorialPlugin)
    add_test(NAME ProcessorTutorialTests COMMAND ProcessorTutorialTests)
    set_tests_properties(ProcessorTutorialTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    )
endif()

termaster_compile_help(
    TARGET ProcessorTutorialHelp
    NAMESPACE org.termaster.example.prefix
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/tutorial-plugin/ProcessorTutorialPlugin/${TERMASTER_PLUGIN_LIBRARY_DIR}/help/ProcessorTutorial.qch"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/Help/manifest.json"
    SCHEMAS
        "example.prefix=${CMAKE_CURRENT_SOURCE_DIR}/Resources/Prefix.yaml"
)
add_dependencies(ProcessorTutorialPlugin ProcessorTutorialHelp)
