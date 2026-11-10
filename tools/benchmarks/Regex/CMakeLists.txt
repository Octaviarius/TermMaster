cmake_minimum_required(VERSION 3.25)
project(TermasterRegexBenchmark LANGUAGES CXX)

set(PCRE2_INCLUDE_DIR "" CACHE PATH "PCRE2 generated interface headers")
set(PCRE2_LIBRARY "" CACHE FILEPATH "PCRE2 8-bit static library")
set(VECTORSCAN_INCLUDE_DIR "" CACHE PATH "Vectorscan src headers")
set(VECTORSCAN_BUILD_DIR "" CACHE PATH "Vectorscan generated headers")
set(VECTORSCAN_LIBRARY "" CACHE FILEPATH "Optional Vectorscan static library")
if(NOT EXISTS "${PCRE2_INCLUDE_DIR}/pcre2.h" OR NOT EXISTS "${PCRE2_LIBRARY}")
    message(FATAL_ERROR "Supply the built PCRE2 8-bit include directory and library")
endif()
add_executable(RegexBenchmark
    RegexBenchmark.cpp
)
target_compile_features(RegexBenchmark PRIVATE
    cxx_std_20
)
target_compile_definitions(RegexBenchmark PRIVATE
    PCRE2_STATIC
)
target_include_directories(RegexBenchmark PRIVATE
    "${PCRE2_INCLUDE_DIR}"
)
target_link_libraries(RegexBenchmark PRIVATE
    "${PCRE2_LIBRARY}"
    psapi
)
if(VECTORSCAN_LIBRARY)
    target_compile_definitions(RegexBenchmark PRIVATE
        TERMASTER_HAVE_VECTORSCAN=1
    )
    target_include_directories(RegexBenchmark PRIVATE
        "${VECTORSCAN_INCLUDE_DIR}"
        "${VECTORSCAN_BUILD_DIR}"
    )
    target_link_libraries(RegexBenchmark PRIVATE
        "${VECTORSCAN_LIBRARY}"
    )
endif()
if(MSVC)
    target_compile_options(RegexBenchmark PRIVATE
        /W4
    )
else()
    target_compile_options(RegexBenchmark PRIVATE
        -Wall
        -Wextra
    )
endif()
