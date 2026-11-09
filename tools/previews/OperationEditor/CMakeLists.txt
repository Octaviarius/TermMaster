cmake_minimum_required(VERSION 3.21)

project(TermasterOperationEditorPreview LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Svg
)
qt_standard_project_setup()

qt_add_executable(TermasterOperationEditorPreview
    OperationEditorPreview.cpp
)
target_compile_features(TermasterOperationEditorPreview PRIVATE cxx_std_20)
target_compile_options(TermasterOperationEditorPreview PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)
target_link_libraries(TermasterOperationEditorPreview PRIVATE
    Qt6::Widgets
    Qt6::Svg
)
set(icon_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/lucide/icons")
qt_add_resources(TermasterOperationEditorPreview "PreviewIcons"
    PREFIX "/icons"
    BASE "${icon_root}"
    FILES
        "${icon_root}/play.svg"
        "${icon_root}/file-plus.svg"
        "${icon_root}/terminal.svg"
        "${icon_root}/sliders-horizontal.svg"
        "${icon_root}/rotate-cw.svg"
        "${icon_root}/scan-text.svg"
        "${icon_root}/folder-open.svg"
)
