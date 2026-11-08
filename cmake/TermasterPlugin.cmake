include_guard(GLOBAL)

# DEPENDENCIES use Termaster<name> packages exporting Termaster::<name> targets.
# Public headers must be authored below HEADER_BASE_DIR/Termaster/<target>.
# Each SDK can be consumed by adding its isolated root to CMAKE_PREFIX_PATH.
function(termaster_add_plugin target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "HEADER_BASE_DIR;OUTPUT_ROOT"
        "SOURCES;PUBLIC_HEADERS;DEPENDENCIES"
    )
    if(ARG_UNPARSED_ARGUMENTS OR ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid arguments to termaster_add_plugin(${target})")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "termaster_add_plugin(${target}) requires SOURCES")
    endif()
    if(NOT TARGET Termaster::PluginApi)
        find_package(TermasterPluginApi CONFIG REQUIRED)
    endif()
    if(NOT ARG_HEADER_BASE_DIR)
        set(ARG_HEADER_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inc")
    endif()
    if(NOT ARG_OUTPUT_ROOT)
        set(ARG_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/plugin")
    endif()
    get_filename_component(header_base "${ARG_HEADER_BASE_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    get_filename_component(output_root "${ARG_OUTPUT_ROOT}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
    )
    set(sdk_root "${output_root}/${target}")
    set(package_dir "${sdk_root}/lib/cmake/Termaster${target}")

    add_library(${target} SHARED
        ${ARG_SOURCES}
        ${ARG_PUBLIC_HEADERS}
    )
    add_library(Termaster::${target} ALIAS ${target})
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PUBLIC
        Termaster::PluginApi
        ${ARG_DEPENDENCIES}
    )
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${header_base}>"
        "$<INSTALL_INTERFACE:include>"
    )
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${sdk_root}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${sdk_root}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${sdk_root}/lib"
    )
    # One SDK root represents the last built configuration.
    foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${sdk_root}/bin"
            LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${sdk_root}/lib"
            ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper} "${sdk_root}/lib"
        )
    endforeach()

    set(header_commands "")
    foreach(header IN LISTS ARG_PUBLIC_HEADERS)
        get_filename_component(header_absolute "${header}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        file(RELATIVE_PATH header_relative "${header_base}/Termaster/${target}" "${header_absolute}")
        if(header_relative MATCHES "^\\.\\.(/|$)" OR IS_ABSOLUTE "${header_relative}")
            message(FATAL_ERROR "Public header ${header} must be below ${header_base}/Termaster/${target}")
        endif()
        if(NOT EXISTS "${header_absolute}")
            message(FATAL_ERROR "Public header does not exist: ${header_absolute}")
        endif()
        get_filename_component(header_subdirectory "${header_relative}" DIRECTORY)
        set(destination "${sdk_root}/include/Termaster/${target}/${header_subdirectory}")
        list(APPEND header_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${header_absolute}" "${destination}"
        )
    endforeach()

    if(header_commands)
        add_custom_target(${target}SdkHeaders
            ${header_commands}
            VERBATIM
        )
        add_dependencies(${target} ${target}SdkHeaders)
    endif()

    set(config "include(CMakeFindDependencyMacro)\nfind_dependency(TermasterPluginApi CONFIG)\n")
    set(interfaces "Termaster::PluginApi")
    foreach(dependency IN LISTS ARG_DEPENDENCIES)
        string(REGEX REPLACE "^.*::" "" dependency_name "${dependency}")
        string(APPEND config "find_dependency(Termaster${dependency_name} CONFIG)\n")
        list(APPEND interfaces "Termaster::${dependency_name}")
    endforeach()
    string(APPEND config
        "get_filename_component(_termaster_sdk_root \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
        "if(NOT TARGET Termaster::${target})\n"
        "    add_library(Termaster::${target} SHARED IMPORTED)\n"
        "    set_target_properties(Termaster::${target} PROPERTIES\n"
        "        INTERFACE_INCLUDE_DIRECTORIES \"\${_termaster_sdk_root}/include\"\n"
        "        INTERFACE_COMPILE_FEATURES cxx_std_20\n"
        "        INTERFACE_LINK_LIBRARIES \"${interfaces}\"\n"
    )
    if(WIN32)
        string(APPEND config
            "        IMPORTED_LOCATION \"\${_termaster_sdk_root}/bin/$<TARGET_FILE_NAME:${target}>\"\n"
            "        IMPORTED_IMPLIB \"\${_termaster_sdk_root}/lib/$<TARGET_LINKER_FILE_NAME:${target}>\"\n"
        )
    else()
        string(APPEND config
            "        IMPORTED_LOCATION \"\${_termaster_sdk_root}/lib/$<TARGET_FILE_NAME:${target}>\"\n"
        )
    endif()
    string(APPEND config "    )\nendif()\nunset(_termaster_sdk_root)\n")
    set(generated_config "${CMAKE_CURRENT_BINARY_DIR}/Termaster${target}-$<CONFIG>Config.cmake")
    file(GENERATE OUTPUT "${generated_config}" CONTENT "${config}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${package_dir}"
            "${sdk_root}/include/Termaster/${target}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${generated_config}"
            "${package_dir}/Termaster${target}Config.cmake"
        VERBATIM
    )
endfunction()
