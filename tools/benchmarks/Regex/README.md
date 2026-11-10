# Regex benchmark

See [methodology, results and limitations](../../../docs/regex-benchmark.md).
This standalone Windows experiment is independent of the application's CMake graph.

```powershell
python tools/benchmarks/Regex/Build.py --interface <physical-adapter-IPv4>
python tools/benchmarks/Regex/Run.py
```

`Build.py` downloads SHA-256-pinned archives only through the supplied physical
adapter and builds Ragel, PCRE2, Vectorscan and the harness with the installed
MinGW toolchain. Dependencies stay under disposable `build/RegexBenchmark`.
`Run.py` checks correctness before collecting five isolated samples per case.
Use `--help` for alternate paths, output directory, repetition count and quick mode.

The harness supports `pcre2`, `pcre2-jit`, `pcre2-dfa`, `vectorscan` and
`vectorscan-pcre2`; the hybrid uses JIT extraction. Example:

```powershell
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;" + $env:PATH
./build/RegexBenchmark/bench-mingw/RegexBenchmark.exe --engine pcre2-dfa --scenario frames --chunk 1 --payload 16384 --mib 1
```

The CMake project can also be built PCRE2-only by omitting the optional
`VECTORSCAN_*` cache arguments. Its flags accept native MSVC, but upstream
Vectorscan 5.4.14 does not currently compile with that toolchain unmodified.
