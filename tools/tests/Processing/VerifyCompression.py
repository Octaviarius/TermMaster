"""Validate captured native Compress packets with Python's independent zlib API."""

import argparse
from pathlib import Path
import zlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    directory = parser.parse_args().directory
    original = (directory / "original.bin").read_bytes()

    for name, bits in (("zlib", 15), ("gzip", 31), ("raw", -15)):
        packed = (directory / f"{name}.bin").read_bytes()
        decoder = zlib.decompressobj(bits)
        actual = decoder.decompress(packed) + decoder.flush()
        if actual != original or not decoder.eof or decoder.unused_data:
            raise SystemExit(f"Invalid native {name} output")
        print(f"{name}: {len(packed)} bytes -> {len(actual)} bytes, exact match")

    print(f"Python zlib runtime: {zlib.ZLIB_RUNTIME_VERSION}")


if __name__ == "__main__":
    main()
