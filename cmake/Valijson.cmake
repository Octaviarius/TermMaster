if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/valijson/include/valijson/schema.hpp")
    message(FATAL_ERROR "Valijson is missing. Run git submodule update --init.")
endif()

# Pinned header-only dependency, consumed without upstream configuration/downloads.
add_library(valijson INTERFACE)

target_include_directories(valijson SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/components/valijson/include"
)

target_compile_definitions(valijson INTERFACE
    VALIJSON_USE_EXCEPTIONS=1
)
