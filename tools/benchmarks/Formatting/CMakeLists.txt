find_package(Qt6 REQUIRED COMPONENTS
    Widgets
)

add_executable(FormattingBenchmark
    FormattingBenchmark.cpp
)

target_compile_features(FormattingBenchmark PRIVATE cxx_std_20)

target_link_libraries(FormattingBenchmark PRIVATE
    Core
    fmt::fmt
)
