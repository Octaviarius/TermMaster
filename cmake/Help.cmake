if(WIN32)
    set(TERMASTER_PLUGIN_LIBRARY_DIR bin)
else()
    set(TERMASTER_PLUGIN_LIBRARY_DIR lib)
endif()

include(CMakeParseArguments)

# Compile module Markdown and generated descriptors into a distributable Qt Help file.
function(termaster_compile_help)
    cmake_parse_arguments(HELP
        "NATIVE"
        "TARGET;OUTPUT_FILE;NAMESPACE"
        "MANIFESTS;SCHEMAS"
        ${ARGN}
    )
    if(NOT HELP_TARGET OR NOT HELP_OUTPUT_FILE OR NOT HELP_NAMESPACE OR NOT HELP_MANIFESTS)
        message(FATAL_ERROR "termaster_compile_help requires TARGET, OUTPUT_FILE, NAMESPACE and MANIFESTS")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Help)
    if(TARGET HelpCompiler)
        set(help_compiler HelpCompiler)
    elseif(TARGET Termaster::HelpCompiler)
        set(help_compiler Termaster::HelpCompiler)
    else()
        message(FATAL_ERROR "The Termaster help compiler is missing from the selected SDK")
    endif()
    find_program(TERMASTER_QHELPGENERATOR
        NAMES qhelpgenerator qhelpgenerator6
        HINTS "${Qt6_DIR}/../../../bin"
        REQUIRED
    )
    set(help_arguments "")
    set(help_inputs "")
    foreach(manifest IN LISTS HELP_MANIFESTS)
        list(APPEND help_arguments --manifest "${manifest}")
        get_filename_component(help_root "${manifest}" DIRECTORY)
        file(GLOB_RECURSE module_help_inputs CONFIGURE_DEPENDS "${help_root}/*")
        list(APPEND help_inputs ${module_help_inputs})
    endforeach()
    foreach(schema IN LISTS HELP_SCHEMAS)
        list(APPEND help_arguments --schema "${schema}")
        string(REGEX REPLACE "^[^=]+=" "" schema_file "${schema}")
        list(APPEND help_inputs "${schema_file}")
    endforeach()
    if(HELP_NATIVE)
        list(APPEND help_arguments --native)
    endif()
    set(help_output "${CMAKE_CURRENT_BINARY_DIR}/Help/${HELP_TARGET}")
    get_filename_component(help_destination "${HELP_OUTPUT_FILE}" DIRECTORY)
    add_custom_command(OUTPUT "${HELP_OUTPUT_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${help_destination}"
        COMMAND $<TARGET_FILE:${help_compiler}>
            ${help_arguments}
            --namespace "${HELP_NAMESPACE}"
            --output "${help_output}"
        COMMAND "${TERMASTER_QHELPGENERATOR}" "${help_output}/help.qhp" -o "${HELP_OUTPUT_FILE}"
        DEPENDS ${help_compiler} ${help_inputs}
        VERBATIM
        COMMENT "Compiling ${HELP_NAMESPACE} help"
    )
    add_custom_target(${HELP_TARGET} DEPENDS "${HELP_OUTPUT_FILE}")
endfunction()
