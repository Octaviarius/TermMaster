cmake_minimum_required(VERSION 3.25)
project(RegexBenchmarkRagel LANGUAGES CXX)

if(NOT RAGEL_SOURCE_DIR)
    message(FATAL_ERROR "Set RAGEL_SOURCE_DIR to the pinned Ragel 6.10 source")
endif()
file(GLOB ragel_sources CONFIGURE_DEPENDS
    "${RAGEL_SOURCE_DIR}/ragel/*.cpp"
)
configure_file(
    RagelConfig.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    COPYONLY
)

add_executable(ragel
    ${ragel_sources}
)
target_include_directories(ragel PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${RAGEL_SOURCE_DIR}/ragel"
    "${RAGEL_SOURCE_DIR}/aapl"
)
set_target_properties(ragel PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED ON
)
if(WIN32)
    target_link_libraries(ragel PRIVATE
        psapi
    )
endif()
