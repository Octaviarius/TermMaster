# Processing performance profile

This profile measures synthetic native processor calls and serialized Lineflow delivery.
No production behavior is changed. See [processing core](processing-core.md) and
[processing nodes](processing-nodes.md) for the runtime contracts.

## Reproduction and scope

The standalone harness in [ProcessingProfile](../tools/benchmarks/ProcessingProfile/CMakeLists.txt)
links the existing MSVC Release `Core.lib` and matching static dependencies. Rebuild Core
from the current checkout before linking. It does not alter the main target graph.

```powershell
. .\.vscode\ExportEnvironment.ps1
cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Release --target Core
cmake -S tools/benchmarks/ProcessingProfile -B build/ProcessingProfile -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$env:TERMASTER_QT_ROOT
cmake --build build/ProcessingProfile
.\build\ProcessingProfile\ProcessingProfile.exe |
    Set-Content -Encoding utf8 docs/benchmarks/processing-profile/ProcessingProfile.csv
python tools/benchmarks/ProcessingProfile/SummarizeProcessingProfile.py `
    docs/benchmarks/processing-profile/ProcessingProfile.csv
```

Each case has one discarded warmup and five recorded repetitions. Packet sizes are
1, 64 and 4096 bytes. Ordinary cases run 20,000 invocations per repetition. Growing
partial-frame cases retain 65,536 bytes, with invocation count adjusted by packet
size. Factories, preparation and cancellation are outside timed sections. Input is
reused through QByteArray implicit sharing; input generation and transport are excluded.
Output callbacks count packets and bytes without storing or rendering them. Bypass
output counts/lengths and partial-frame zero-output behavior are checked. Every
invocation must be accepted and complete without error; cancellation must acknowledge.

Lineflow `lineflow-bypass` results include serialized caller event dispatch through
`QCoreApplication::processEvents`. Paired `lineflow-event-driven-bypass` cases run
one QEventLoop, submitting the next packet directly from the previous completion
callback. Both include Qt event dispatch and are end-to-end microbenchmark latency,
not isolated scheduler CPU time or GUI throughput. The harness does not model concurrent
ports, real I/O, widget rendering, event-loop contention or slow output consumers.
Dense Framer uses 8192 maxChunks to permit 2048 two-byte frames from a 4096-byte
packet; other ordinary limits are defaults. Partial-frame cases use a 2 MiB limit.
Input MiB/s counts source bytes, not expanded output. Bypass bandwidth is a reference
sharing rate, not memory-copy bandwidth. Compression input consists of repeated `a`
bytes and is intentionally highly compressible, so it cannot represent arbitrary data.

## Measurements on 2026-11-10

Host: Windows 11, AMD Ryzen 9 5900X (12 cores / 24 logical processors), MSVC
19.44.35225 x64, Qt 6.12.0, Release. Core was freshly rebuilt from the current
checkout; the isolated harness was relinked after that build. Compilation was idle
during timing. [Environment and SHA-256 identities](benchmarks/processing-profile/Environment.json),
[raw repetitions](benchmarks/processing-profile/ProcessingProfile.csv) and the
[full median/range table](benchmarks/processing-profile/Summary.md) preserve the evidence.

| Case | 1-byte packet | 64-byte packet | 4096-byte packet |
| --- | ---: | ---: | ---: |
| Direct Bypass | 0.257 us | 0.283 us | 0.300 us |
| Lineflow, 1 Bypass, event-driven | 15.218 us | 15.989 us | 16.601 us |
| Lineflow, 16 Bypass, event-driven | 25.653 us | 25.941 us | 27.049 us |
| Native HEX encoder | 0.499 us | 1.837 us | 75.867 us |
| Native Base64 encoder | 0.514 us | 1.181 us | 34.545 us |
| Native compression, repeated `a` | 2.102 us | 2.414 us | 16.726 us |
| Delimiter Framer, 4096-byte frames | 0.400 us | 0.751 us | 33.416 us |
| Delimiter Framer, 2-byte frames | n/a | 3.240 us | 177.815 us |
| Pattern Framer, growing unterminated match | 15.189 us | 17.242 us | 17.688 us |
| Delimiter Framer, growing overlapping prefix | 0.265 us | 0.876 us | 43.688 us |

All cells are median elapsed time per accepted input packet. In the 1-byte partial
pattern case, delivering only 64 KiB takes about 0.995 seconds per repetition,
versus 17.36 milliseconds for the delimiter implementation. At 4096-byte packets,
the same input needs 16 calls and about 0.283 milliseconds for Pattern Framer.
This is a concrete packet-size sensitivity in a retained partial-match workload;
the matrix does not establish general regex complexity for all expressions.

The event-driven one-stage Lineflow baseline is roughly 15-17 us regardless of
packet size. A 4096-byte input therefore reaches about 235 MiB/s versus 3.82 MiB/s
for 64-byte packets. The paired processEvents producer gives similar results
(16-17 us at depth one), so that caller difference does not explain the entire
observed gap from direct Bypass. The gap includes runtime wrappers, callbacks,
queueing and event dispatch; no sampling or allocator instrumentation attributes
it to a single function.

At 4096 bytes, HEX reaches 51.49 MiB/s and Base64 113.08 MiB/s; compression reaches
233.54 MiB/s on this highly compressible input. Delimiter Framer reaches 116.90
MiB/s with 4096-byte frames but 21.97 MiB/s with dense two-byte frames. These
workload comparisons identify priorities, not interchangeable implementations.

## Prioritized investigation candidates

1. **Pattern Framer retained partial matches.**
   `src/Core/src/Processing/Nodes/PatternFramer.cpp:328` passes the entire retained
   subject to JIT matching; line 335 retains the viable partial match's start.
   For `(?<payload>a+)z` and an unterminated run of `a`, that start remains zero.
   Repeated small packets can therefore rescan a growing prefix. Investigate bounded
   incremental/resume policies where supported, or explicit framing guidance and
   time/size limits. Preserve regex semantics, lookbehind, `\\G`, partial matching,
   exact output and atomic failure. The 20 ms check is between matching calls and
   does not preempt a single JIT invocation; PCRE2 internal limits remain important.
2. **Small-packet Lineflow scheduling and allocations.**
   `Lineflow.cpp:445` allocates a shared step budget for each accepted root;
   line 534 posts a zero-delay callback; line 548 allocates a blocked-stage vector
   each pump. Compare the paired event-driven producer and measure allocation counts before changing
   scheduling. Reuse bounded scratch state or reduce redundant dispatch only after
   preserving fairness, cancellation, callback reentry and packet boundaries.
3. **Delimiter Framer per-byte append and per-frame allocation.**
   `Nodes/Framer.cpp:177` visits each byte; line 185 appends one byte at a time;
   lines 158–175 move each completed buffer into a staged output list. Dense tiny
   frames incur thousands of allocations/callbacks. Explore span append/search and
   reusable bounded staging, retaining KMP overlap semantics, split delimiters,
   includeEnding, empty frames and all-or-nothing invocation output. Never coalesce
   configured frame boundaries merely to improve the benchmark.
4. **HEX encoding output construction.**
   `Nodes/HexEncoder.cpp:47` loops over bytes and lines 49-50 perform three bounded
   append operations per byte with the default space separator. The 4096-byte case
   expands to 12,287 output bytes and is the slowest of the three measured encoder
   cases. Investigate validating the exact output size once, reserving bounded
   storage and filling spans. Preserve separator semantics, arbitrary bytes, empty
   packets and error-before-publication. This is a source-backed optimization
   candidate; no allocator or function-level profile proves append is dominant.
5. **Lineflow queue search under pressure (hypothesis).**
   `Lineflow.cpp:553` scans for runnable work, line 564 erases it, and line 682
   scans pending roots. These are candidates for fan-out/backpressure workloads,
   not established bottlenecks in the serialized bypass matrix. Add bounded dense
   fan-out and blocked-port workloads before choosing another queue representation.

No production optimization is implemented by this profiling work.
