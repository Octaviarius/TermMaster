if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/fmt/CMakeLists.txt")
    message(FATAL_ERROR
        "fmt is missing. Run git submodule update --init components/fmt."
    )
endif()

function(termaster_add_fmt)
    set(BUILD_SHARED_LIBS OFF)
    set(FMT_DOC OFF CACHE BOOL "Build fmt documentation" FORCE)
    set(FMT_TEST OFF CACHE BOOL "Build fmt tests" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "Install fmt" FORCE)
    add_subdirectory(components/fmt EXCLUDE_FROM_ALL)
endfunction()

termaster_add_fmt()
