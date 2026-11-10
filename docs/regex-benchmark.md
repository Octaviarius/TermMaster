# Streaming byte-regex benchmark

Date: 2026-11-10. This is an isolated research benchmark, not a new application
processor or a dependency added to Termaster's production build. No runtime engine
has been selected automatically.

## Toolchain and reproducibility

- Windows 11 build 28000; AMD Ryzen 9 5900X, 12 cores / 24 logical processors.
- Both libraries and the harness: MinGW GCC 13.1.0 x64, Release, `-O3 -DNDEBUG`.
- Vectorscan 5.4.14: static, non-fat `core-avx2` target; AVX-512 disabled.
- PCRE2 10.49: 8-bit static library with JIT, UTF mode disabled.
- Build-only dependencies: Boost 1.85.0 headers, Ragel 6.10; PCRE2's pinned SLJIT.
- Five fresh processes per measurement, sequential execution, logical CPU 4 affinity.
  A calibration selects repeated scans targeting at least 120 ms, capped at 128.
- Corpus generation and pattern compilation are excluded from scan timing. Timing
  includes packet copying, buffer maintenance, callbacks, group extraction, exact
  range/payload checks and checksums. These are validated pipeline measurements,
  not isolated regex instruction throughput.

[Build script](../tools/benchmarks/Regex/Build.py),
[runner](../tools/benchmarks/Regex/Run.py),
[harness](../tools/benchmarks/Regex/RegexBenchmark.cpp) and
[pinned archives with SHA-256](../tools/benchmarks/Regex/Sources.json) are tracked.
Downloads explicitly bind a user-supplied physical-adapter IPv4 address, bypass
proxies, and verify archive hashes. CMake never downloads dependencies.

From the repository root, with Python 3.11+ and the Qt tools installed:

```powershell
python tools/benchmarks/Regex/Build.py --interface <physical-adapter-IPv4>
python tools/benchmarks/Regex/Run.py
```

The scripts accept alternate MinGW/CMake/Ninja paths. The build is isolated under
`build/RegexBenchmark`. Results are written to `docs/benchmarks/regex` by default;
use the runner's `--output` to retain an independent run. The build script was
executed successfully against the downloaded, hash-verified source cache.

## What is matched

The PCRE2 expression is:

```regex
\xA5\x5A(?<payload>[^\xFF]{32,})\xFF\x00
```

Vectorscan detects the equivalent expression without the named group and with
leftmost start-of-match reporting, streaming mode and the large SOM horizon.
It does not provide captured groups. Its capture variant passes each detected
frame through the same PCRE2 JIT extractor, validating full candidate boundaries.

Three deterministic binary corpora exercise complete frames, random noise, and
near misses (valid prefix/body followed by `FF 01` instead of `FF 00`). Data
contains NUL and bytes above 0x7F without text conversion. Payload lengths are 64
bytes or 16 KiB. The harness's external payload contract is at most 64 KiB; the
regex itself has no upper bound. Noise and payload exclude `A5`, deliberately
avoiding nested/overlapping candidate starts. Results do not establish semantic
equivalence for arbitrary regexes, overlapping matches, lookaround or backreferences.

Packets are 1, 17, 256, 4096 or 65536 bytes. Bytewise input splits every header,
payload and terminator across calls. Most corpora occupy 8 MiB; long-frame tests
with 1/17-byte packets use 1 MiB to bound quadratic rescanning time. Each iteration
checks every frame range and captured payload against an independent generated
oracle. 110 additional correctness jobs cover packet sizes 1/2/3/17/257/4096/65536
and long bytewise frames. All passed, as did all 500 timed jobs.

The DFA alternative was included to avoid presenting repeated partial matching as
PCRE2's only streaming strategy. It preserves the DFA workspace between segments,
then uses JIT for groups. On continuation failure it retries later starts over
retained bytes; it does not discard potentially valid candidates. An untimed
recovery fixture checks a failed candidate followed by a valid frame with packet
sizes 1/64/128. DFA does not support the full ordinary PCRE2 feature set.

## Results

All speeds below are medians in MiB/s; higher is better. Capture-capable pipelines
are the relevant comparison for extracting a selected part of each frame.

| Payload | Packet | PCRE2 JIT + groups | PCRE2 DFA + JIT groups | Vectorscan + JIT groups |
|---|---:|---:|---:|---:|
| 64 B | 1 | 28.17 | 22.32 | 14.30 |
| 64 B | 256 | 915.99 | 198.84 | 336.15 |
| 64 B | 4096 | 1026.46 | 205.62 | 415.70 |
| 16 KiB | 1 | 0.54 | 19.24 | 12.79 |
| 16 KiB | 17 | 9.13 | 87.48 | 141.15 |
| 16 KiB | 256 | 114.59 | 115.76 | 647.71 |
| 16 KiB | 4096 | 617.67 | 118.51 | 800.99 |
| 16 KiB | 65536 | 840.63 | 118.45 | 808.63 |

On 16 KiB frames with bytewise input, ordinary PCRE2 partial matching repeatedly
submits retained prefixes: about 8.46 billion subject bytes for 1 MiB of actual
input. This counter measures API subject lengths, not actual internal memory reads.
DFA continuation and Vectorscan avoid the same repeated-prefix submission pattern.

Detection-only Vectorscan reaches 11,492 MiB/s for 16 KiB payloads / 4096-byte
packets, but adding payload extraction and validation lowers the complete pipeline
to 801 MiB/s. The detection-only figure must not be compared as if it included groups.
For near misses with 4096-byte packets, PCRE2 JIT reaches 2958 MiB/s versus 891 for
the Vectorscan capture pipeline. Pattern and packet distribution materially change
the outcome. Raw min/max variation is preserved in the CSV, including the roughly
11–15% spread in some bytewise DFA cases.

## Memory and compilation

Component accounting below is for 64-byte frames / 1-byte packets. It sums library
reported compiled code/database, match data, cached match frames, configured DFA
workspace, Vectorscan scratch/stream state and input-buffer capacity.

| Pipeline | Accounted components and buffer | Peak process working set | Pattern compilation |
|---|---:|---:|---:|
| PCRE2 interpreter + groups | 20.40 KiB | 14.46 MiB | 0.054 ms |
| PCRE2 JIT + groups | 2.16 KiB | 14.66 MiB | 0.195 ms |
| PCRE2 DFA continuation + JIT groups | 18.84 KiB | 14.66 MiB | 0.211 ms |
| Vectorscan detection only | 5.57 KiB | 16.77 MiB | 2.725 ms |
| Vectorscan + PCRE2 JIT groups | 7.92 KiB | 17.11 MiB | 2.844 ms |

Component accounting is not complete RAM usage: it excludes allocator overhead,
PCRE2 match contexts/JIT stack and potential temporary DFA allocations. The DFA
workspace is configured as 4096 integers (16 KiB), not a claimed minimum. Whole-
process figures include the 8 MiB corpus, expected offsets, DLLs, compilation and
allocator retention; peak working set includes generation/compilation and is not
incremental per-stream memory. Vectorscan database is 2928 B, scratch is 2709 B,
and stream state is 71 B for this expression. Database sharing and per-thread
scratch reuse were not measured across multiple terminals.

For bytewise 16 KiB frames, accounted totals are 18.10 KiB for PCRE2 JIT, 34.84 KiB
for DFA + groups, and 39.92 KiB for Vectorscan + groups. Buffers, rather than just
engine state, matter. In no-match workloads the hybrid's bounded sliding capture
window grows larger; that is this adapter's retention strategy, not an inherent
minimum imposed by Vectorscan. It retires completed frames once per packet and
compacts unmatched noise in batches, retaining a full possible 64 KiB candidate.

## Native MSVC result

PCRE2 10.49 with JIT and the harness compiled successfully with MSVC 19.44 x64
Release. Bytewise short-frame JIT and long-frame DFA capture smoke checks also
passed with the same expected frame/payload checksums as MinGW. Vectorscan
5.4.14 configured with available Boost/Ragel, but the upstream native MSVC build
failed when `cl.exe` received GCC flags (`D8021: invalid numeric argument '/Wextra'`).
See [the captured probe](benchmarks/regex/MsvcProbe.txt) and
[MSVC correctness smoke checks](benchmarks/regex/MsvcSmoke.jsonl). No upstream Vectorscan
source was modified. Upstream's documented Windows instructions use MSYS2;
MinGW measurements are not native MSVC integration acceptance.

A native MSVC fork would first require compiler-specific build options, followed
by checking GNU attributes/intrinsics and running upstream tests. Successful MinGW
compilation does not prove that a flag-only MSVC patch is sufficient. No fork or
production dependency was created for this experiment.

## Decision support

- PCRE2 JIT is the simpler candidate for short frames and rich capture syntax.
- PCRE2 DFA continuation plus JIT extraction avoids repeated-prefix scanning for
  compatible patterns without another runtime library; its syntax is restricted.
- Vectorscan plus PCRE2 is attractive for long frames arriving in medium-sized
  packets in this workload. Its advantage is not universal, and it still needs a
  second stage for groups and a separate MSVC port if native compilation is required.
- Further acceptance should include actual intended framing patterns, ambiguous
  starts and match-selection rules before committing to an engine. These results
  do not select a default engine automatically.

Durable data: [summary CSV](benchmarks/regex/Summary.csv),
[500 raw samples](benchmarks/regex/RawResults.jsonl),
[110 correctness jobs](benchmarks/regex/Correctness.json),
[environment](benchmarks/regex/Environment.json).

Primary references: [PCRE2 segmented matching](https://www.pcre.org/current/doc/html/pcre2partial.html),
[PCRE2 DFA API](https://www.pcre.org/current/doc/html/pcre2_dfa_match.html),
[Vectorscan 5.4.14](https://github.com/VectorCamp/vectorscan/tree/vectorscan/5.4.14).
