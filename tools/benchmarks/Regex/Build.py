"""Build isolated, pinned regex benchmark dependencies; never downloads in CMake."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", required=True, help="IPv4 address of a physical adapter")
    parser.add_argument("--mingw", type=Path, default=Path("C:/Qt/Tools/mingw1310_64"))
    parser.add_argument("--cmake", default="C:/Qt/Tools/CMake_64/bin/cmake.exe")
    parser.add_argument("--ninja", default="C:/Qt/Tools/Ninja/ninja.exe")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    build = root / "build/RegexBenchmark"
    sources = build / "sources"
    sources.mkdir(parents=True, exist_ok=True)

    command = (
        "$physical = @(Get-NetAdapter -Physical | Select-Object -ExpandProperty ifIndex); "
        "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { "
        "$physical -contains $_.InterfaceIndex } | Select-Object -ExpandProperty IPAddress | ConvertTo-Json"
    )
    addresses = json.loads(subprocess.check_output(["pwsh", "-NoProfile", "-Command", command], text=True))
    if isinstance(addresses, str):
        addresses = [addresses]
    if args.interface not in addresses:
        raise SystemExit("The supplied address does not belong to a physical adapter")

    for entry in json.loads((here / "Sources.json").read_text(encoding="utf-8")):
        archive = sources / entry["file"]
        if not archive.exists():
            partial = archive.with_suffix(archive.suffix + ".partial")
            subprocess.run([
                "curl.exe", "--interface", args.interface, "--noproxy", "*", "--fail",
                "--location", "--retry", "2", "--connect-timeout", "20", "--max-time", "300",
                entry["url"], "--output", str(partial),
            ], check=True)
            partial.replace(archive)
        with archive.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        if digest != entry["sha256"]:
            raise SystemExit(f"SHA-256 mismatch: {archive.name}")
        destination = sources / entry["directory"]
        marker = destination / ".termaster-benchmark-source"
        if marker.exists():
            continue
        destination.mkdir(parents=True, exist_ok=True)
        if entry.get("strip"):
            extract = ["tar", "-xf", str(archive), "-C", str(destination), "--strip-components=1"]
        else:
            extract = ["tar", "-xf", str(archive), "-C", str(sources)]
            if entry.get("member"):
                extract.append(entry["member"])
        subprocess.run(extract, check=True)
        marker.write_text(entry["sha256"] + "\n", encoding="utf-8")

    env = os.environ.copy()
    env["PATH"] = str(args.mingw / "bin") + os.pathsep + str(Path(args.ninja).parent) + os.pathsep + env["PATH"]

    def run(command, log):
        command = [str(argument).replace("\\", "/") for argument in command]
        with (build / log).open("w", encoding="utf-8") as output:
            result = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f"Command failed; see {build / log}")

    def configure(source, name, extra):
        run([args.cmake, "-S", str(source), "-B", str(build / name), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
             f"-DCMAKE_C_COMPILER={args.mingw / 'bin/gcc.exe'}",
             f"-DCMAKE_CXX_COMPILER={args.mingw / 'bin/g++.exe'}", *extra], name + "-configure.log")

    def compile_target(name, target):
        run([args.cmake, "--build", str(build / name), "--target", target,
             "--parallel", str(args.jobs)], name + "-build.log")

    configure(here / "Ragel", "ragel-mingw", [f"-DRAGEL_SOURCE_DIR={sources / 'ragel-6.10'}"])
    compile_target("ragel-mingw", "ragel")
    configure(sources / "pcre2-pcre2-10.49", "pcre2-mingw", [
        "-DPCRE2_BUILD_PCRE2_8=ON", "-DPCRE2_BUILD_PCRE2_16=OFF", "-DPCRE2_BUILD_PCRE2_32=OFF",
        "-DPCRE2_SUPPORT_JIT=ON", "-DPCRE2_BUILD_TESTS=OFF", "-DPCRE2_BUILD_PCRE2GREP=OFF",
        "-DBUILD_STATIC_LIBS=ON", "-DBUILD_SHARED_LIBS=OFF",
    ])
    compile_target("pcre2-mingw", "pcre2-8-static")
    configure(sources / "vectorscan-vectorscan-5.4.14", "vectorscan-mingw", [
        f"-DBOOST_ROOT={sources / 'boost_1_85_0'}", f"-DRAGEL={build / 'ragel-mingw/ragel.exe'}",
        "-DBUILD_STATIC_LIBS=ON", "-DBUILD_SHARED_LIBS=OFF", "-DFAT_RUNTIME=OFF",
        "-DBUILD_AVX2=ON", "-DBUILD_AVX512=OFF", "-DBUILD_AVX512VBMI=OFF",
        "-DBUILD_UNIT=OFF", "-DBUILD_TOOLS=OFF", "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_BENCHMARKS=OFF", "-DBUILD_DOC=OFF", "-DBUILD_CHIMERA=OFF",
    ])
    compile_target("vectorscan-mingw", "hs")
    configure(here, "bench-mingw", [
        f"-DPCRE2_INCLUDE_DIR={build / 'pcre2-mingw/interface'}",
        f"-DPCRE2_LIBRARY={build / 'pcre2-mingw/libpcre2-8.a'}",
        f"-DVECTORSCAN_INCLUDE_DIR={sources / 'vectorscan-vectorscan-5.4.14/src'}",
        f"-DVECTORSCAN_BUILD_DIR={build / 'vectorscan-mingw'}",
        f"-DVECTORSCAN_LIBRARY={build / 'vectorscan-mingw/lib/libhs.a'}",
    ])
    compile_target("bench-mingw", "RegexBenchmark")
    print(build / "bench-mingw/RegexBenchmark.exe")


if __name__ == "__main__":
    main()
