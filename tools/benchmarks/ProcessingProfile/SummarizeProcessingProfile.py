"""Summarize the standalone processing microbenchmark CSV without dependencies."""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

source = Path(sys.argv[1])
groups = defaultdict(list)
with source.open(encoding="utf-8-sig", newline="") as stream:
    for row in csv.DictReader(stream):
        groups[(row["case"], int(row["packet_bytes"]))].append(row)
print("| Case | Packet bytes | Median ns/packet | Input MiB/s | Min-max ns/packet |")
print("| --- | ---: | ---: | ---: | ---: |")
for (name, size), rows in groups.items():
    assert len(rows) == 5, (name, size, len(rows))
    times = [int(row["elapsed_ns"]) / int(row["iterations"]) for row in rows]
    median = statistics.median(times)
    print(f"| {name} | {size} | {median:.1f} | {size * 1e9 / median / 1048576:.2f} | {min(times):.1f}-{max(times):.1f} |")
