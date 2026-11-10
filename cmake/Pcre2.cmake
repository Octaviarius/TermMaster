if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/pcre2/src/pcre2_compile.c"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/pcre2/deps/sljit/sljit_src/sljitLir.c")
    message(FATAL_ERROR
        "PCRE2/SLJIT is missing. Run git submodule update --init --recursive components/pcre2."
    )
endif()

# Keep upstream's generic options local; dependency retrieval is an explicit Git step.
function(termaster_add_pcre2)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(PCRE2_BUILD_PCRE2_8 ON)
    set(PCRE2_BUILD_PCRE2_16 OFF)
    set(PCRE2_BUILD_PCRE2_32 OFF)
    set(PCRE2_SUPPORT_JIT ON CACHE BOOL "Build PCRE2 JIT for byte pattern framing" FORCE)
    set(PCRE2_BUILD_TESTS OFF)
    set(PCRE2_BUILD_PCRE2GREP OFF)
    set(PCRE2_SHOW_REPORT OFF)
    add_subdirectory(components/pcre2 EXCLUDE_FROM_ALL)
endfunction()

termaster_add_pcre2()
