qt_add_executable(HelpCompiler
    HelpCompiler.cpp
)
target_compile_features(HelpCompiler PRIVATE cxx_std_20)
target_link_libraries(HelpCompiler PRIVATE
    Core
    Qt6::Gui
)
set_target_properties(HelpCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin/PluginApi/bin"
)
