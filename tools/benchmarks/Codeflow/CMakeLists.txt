qt_add_executable(CodeflowProfile
    CodeflowProfile.cpp
)
target_compile_features(CodeflowProfile PRIVATE cxx_std_20)
target_compile_definitions(CodeflowProfile PRIVATE
    CODEFLOW_PROFILE_CONFIGURATION="$<CONFIG>"
)
target_link_libraries(CodeflowProfile PRIVATE
    Core
    Qt6::Core
)
