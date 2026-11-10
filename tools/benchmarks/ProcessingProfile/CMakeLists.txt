cmake_minimum_required(VERSION 3.21)
project(ProcessingProfile LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Svg
    ZlibPrivate
)
get_filename_component(REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(TERMASTER_RELEASE_BUILD
    "${REPOSITORY}/build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release"
    CACHE PATH "Existing matching Release build"
)
add_executable(ProcessingProfile
    ProcessingProfile.cpp
)

target_include_directories(ProcessingProfile PRIVATE
    "${REPOSITORY}/src/Core/inc"
    "${REPOSITORY}/src/Core/src/Processing"
)

target_link_libraries(ProcessingProfile PRIVATE
    "${TERMASTER_RELEASE_BUILD}/src/core/Core.lib"
    "${TERMASTER_RELEASE_BUILD}/components/yaml-cpp/yaml-cpp.lib"
    "${TERMASTER_RELEASE_BUILD}/components/pcre2/pcre2-8-static.lib"
    "${TERMASTER_RELEASE_BUILD}/components/fmt/fmt.lib"
    Qt6::Core
    Qt6::Widgets
    Qt6::Svg
    Qt6::ZlibPrivate
    Mincore
)
