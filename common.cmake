cmake_minimum_required(VERSION 3.3)

function(define_option option_name)
    cmake_parse_arguments(${option_name} "" "DESCRIPTION;DEFAULT;TYPE" "OPTIONS" ${ARGN})
    
    if (${${option_name}_DEFAULT} IN_LIST ${option_name}_OPTIONS)    
        if (NOT DEFINED ${option_name}_TYPE)
            set(${option_name}_TYPE STRING)
        endif()

        set(${option_name} ${${option_name}_DEFAULT} CACHE ${${option_name}_TYPE} ${${option_name}_DESCRIPTION})

        if (${option_name}_TYPE STREQUAL "STRING")
            set_property(CACHE ${option_name} PROPERTY STRINGS ${${option_name}_OPTIONS})
        endif()

        
        get_property(g_options_list GLOBAL PROPERTY GLOBAL_OPTIONS_LIST)
        if (NOT DEFINED g_options_list)
            set(g_options_list "")
        endif()
        list(APPEND g_options_list ${option_name})
        set_property(GLOBAL PROPERTY GLOBAL_OPTIONS_LIST ${g_options_list})

        message(STATUS "Defined option(${${option_name}_TYPE}) ${option_name}=${${option_name}}")
    else()
        message(FATAL_ERROR "Option(${${option_name}_TYPE}) ${option_name} has default value ${${option_name}_DEFAULT} is not 'within ${${option_name}_OPTIONS}'")
    endif()

endfunction()


function(define_string_option option_name)
    cmake_parse_arguments(${option_name} "" "DESCRIPTION;DEFAULT" "OPTIONS" ${ARGN})
    define_option(${option_name} DESCRIPTION ${${option_name}_DESCRIPTION} DEFAULT ${${option_name}_DEFAULT} TYPE STRING OPTIONS ${${option_name}_OPTIONS})
endfunction()

function(define_bool_option option_name)
    cmake_parse_arguments(${option_name} "" "DESCRIPTION;DEFAULT" "" ${ARGN})
    define_option(${option_name} DESCRIPTION ${${option_name}_DESCRIPTION} DEFAULT ${${option_name}_DEFAULT} TYPE BOOL OPTIONS OFF ON)
endfunction()

function(show_variables_table)
    cmake_parse_arguments(ARG "" "SPAN;SEP;MSGMODE;PROPERTY" "VARIABLES" ${ARGN})

    if (NOT DEFINED ARG_SPAN)
        set(ARG_SPAN 4)
    endif()
    
    if (NOT DEFINED ARG_SEP)
        set(ARG_SEP ".")
    endif()
    
    if (NOT DEFINED ARG_MSGMODE)
        set(ARG_MSGMODE "")
    endif()

    # calc max variable name length
    set(max_len 0)
    foreach(opt ${ARG_VARIABLES})
        string(LENGTH ${opt} cur_len)

        if (${cur_len} GREATER ${max_len})
            set(max_len ${cur_len})
        endif()
    endforeach()

    foreach(opt ${ARG_VARIABLES})
        string(LENGTH ${opt} cur_len)
        math(EXPR seps_count "${max_len}-${cur_len}+${ARG_SPAN}")
        string( REPEAT ${ARG_SEP} ${seps_count} seps)

        if (DEFINED ARG_PROPERTY)
            get_property(var_prop CACHE ${opt} PROPERTY ${ARG_PROPERTY})
            message(${ARG_MSGMODE} "${opt}${seps}${var_prop}")
        else()
            message(${ARG_MSGMODE} "${opt}${seps}${${opt}}")
        endif()

    endforeach()
endfunction()


function(show_all_options)
    cmake_parse_arguments(ARG "" "SPAN;SEP;MSGMODE" "" ${ARGN})    
    get_property(g_options_list GLOBAL PROPERTY GLOBAL_OPTIONS_LIST)
    show_variables_table(SPAN ${ARG_SPAN} SEP ${ARG_SEP} MSGMODE ${ARG_MSGMODE} VARIABLES ${g_options_list})
endfunction()


function(show_options_help)
    cmake_parse_arguments(ARG "" "SPAN;SEP;MSGMODE" "" ${ARGN})    
    get_property(g_options_list GLOBAL PROPERTY GLOBAL_OPTIONS_LIST)
    show_variables_table(SPAN ${ARG_SPAN} SEP ${ARG_SEP} MSGMODE ${ARG_MSGMODE} PROPERTY HELPSTRING VARIABLES ${g_options_list})
endfunction()
