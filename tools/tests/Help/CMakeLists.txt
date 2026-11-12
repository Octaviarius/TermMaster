find_package(Qt6 REQUIRED COMPONENTS
    Test
)

qt_add_executable(HelpSourceTests
    HelpSourceTests.cpp
)
target_compile_features(HelpSourceTests PRIVATE cxx_std_20)
target_link_libraries(HelpSourceTests PRIVATE
    Core
    Qt6::Test
)
add_test(NAME HelpSourceTests COMMAND HelpSourceTests)
set_tests_properties(HelpSourceTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
)

qt_add_executable(HelpBrowserTests
    HelpBrowserTests.cpp
)
target_compile_features(HelpBrowserTests PRIVATE cxx_std_20)
target_compile_definitions(HelpBrowserTests PRIVATE
    HELP_MAIN_QCH="${CMAKE_BINARY_DIR}/help/Termaster.qch"
    HELP_PLUGIN_QCH="${CMAKE_BINARY_DIR}/examples/ProcessorPlugin/tutorial-plugin/ProcessorTutorialPlugin/${TERMASTER_PLUGIN_LIBRARY_DIR}/help/ProcessorTutorial.qch"
)
target_link_libraries(HelpBrowserTests PRIVATE
    Core
    Qt6::Test
)
add_dependencies(HelpBrowserTests
    TermasterHelp
    ProcessorTutorialHelp
)
add_test(NAME HelpBrowserTests COMMAND HelpBrowserTests)
set_tests_properties(HelpBrowserTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120
)
