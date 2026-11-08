"""Verify cross-DLL C++ calls and Qt plugin loading on Windows."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--qt-bin", type=Path, required=True)
    parser.add_argument("--external-consumer", type=Path)
    parser.add_argument("--without-api-build", type=Path)
    inspector = parser.add_mutually_exclusive_group(required=True)
    inspector.add_argument("--objdump", type=Path)
    inspector.add_argument("--dumpbin", type=Path)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    qt_bin = args.qt_bin.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    executable = build / "PluginDependencyProbe.exe"
    provider = build / "plugins/Provider.dll"
    consumer = build / "plugins/Consumer.dll"

    dumps = {}
    for name, library in [("provider", provider), ("consumer", consumer), ("host", executable)]:
        command = ([str(args.objdump.resolve(strict=True)), "-p", str(library)] if args.objdump else
                   [str(args.dumpbin.resolve(strict=True)), "/exports" if name == "provider" else "/imports",
                    str(library)])
        result = subprocess.run(command,
                                check=True, capture_output=True, text=True, timeout=10)
        dumps[name] = result.stdout
    if args.objdump:
        exports = re.findall(r"^\s*\[\s*\d+\]\s+(\S+)\s*$", dumps["provider"], re.MULTILINE)
    else:
        exports = re.findall(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)",
                             dumps["provider"], re.MULTILINE)
    require(any(s == "_Z11providerAddii" or s.startswith("?providerAdd@@") for s in exports),
            "Provider does not export the C++ free function")
    require(any(s == "_ZN12ProviderMath8multiplyEii" or s.startswith("?multiply@ProviderMath@@")
                for s in exports), "Provider does not export the C++ class method")
    for symbol in ["qt_plugin_instance", "qt_plugin_query_metadata_v2"]:
        require(symbol in exports, f"Provider does not export {symbol}")
    require("Provider.dll" in dumps["consumer"], "Consumer has no Provider DLL import")
    require("Provider.dll" not in dumps["host"] and "Consumer.dll" not in dumps["host"],
            "Host must not link either plugin at build time")

    evidence = {"provider_exports": exports,
                "consumer_imports_provider": True,
                "host_imports_plugins": False,
                "cases": []}
    target_record = build / "ProbeTargets.json"
    if target_record.is_file():
        evidence["cmake_targets"] = json.loads(target_record.read_text(encoding="utf-8"))
    if any("ProviderAccumulator" in symbol for symbol in exports):
        prefixes = ["??0ProviderAccumulator@@", "??1ProviderAccumulator@@", "?add@ProviderAccumulator@@"]
        imported_methods = []
        for prefix in prefixes:
            matching = [symbol for symbol in exports if symbol.startswith(prefix)]
            require(len(matching) == 1 and matching[0] in dumps["consumer"],
                    f"Consumer does not import the ordinary class operation {prefix}")
            imported_methods.extend(matching)
        evidence["consumer_class_imports"] = imported_methods
    cases = [
        ("provider-first", True, True, False, True),
        ("consumer-first-native-autoload", False, True, True, True),
        ("consumer-first-no-search-path", False, True, False, False),
        ("missing-provider", False, False, True, False),
    ]
    for name, provider_first, include_provider, add_search_path, success in cases:
        with tempfile.TemporaryDirectory(prefix=name + "-", dir=output) as temporary:
            directory = Path(temporary).resolve()
            require(directory.is_relative_to(output), "Case directory escaped output directory")
            shutil.copy2(consumer, directory / "Consumer.dll")
            if include_provider:
                shutil.copy2(provider, directory / "Provider.dll")
            env = dict(os.environ)
            env["QT_FORCE_STDERR_LOGGING"] = "1"
            # Exclude any inherited plugin directory and control DLL discovery explicitly.
            env["PATH"] = str(qt_bin)
            if add_search_path:
                env["PATH"] += os.pathsep + str(directory)
            command = [str(executable), "--plugin-dir", str(directory)]
            if provider_first:
                command.append("--provider-first")
            completed = subprocess.run(command, env=env, cwd=output, capture_output=True,
                                       text=True, encoding="utf-8", timeout=15)
            data = json.loads(completed.stdout)
            require(completed.returncode == (0 if success else 1),
                    f"{name}: unexpected exit {completed.returncode}: {completed.stderr}")
            require(data["consumer_metadata"]["requires"] == ["probe.provider"],
                    f"{name}: missing embedded dependency metadata")
            require(not data["provider_mapped_before_metadata"]
                    and not data["provider_mapped_after_metadata"]
                    and not data["consumer_loaded_after_metadata"],
                    f"{name}: reading metadata loaded a DLL")
            require(data["consumer_loaded"] == success, f"{name}: unexpected consumer load state")
            if success:
                require(data["consumer_result"] == 84, f"{name}: cross-DLL call failed")
                require(data["provider_mapped_after_consumer"], f"{name}: provider DLL is not mapped")
                require("Consumer root constructed" in completed.stderr,
                        f"{name}: consumer plugin root missing")
                require(("Provider root constructed" in completed.stderr) == provider_first,
                        f"{name}: unexpected provider plugin root lifecycle")
                if provider_first:
                    require(data["provider_result"] == 42, f"{name}: provider calculation failed")
            else:
                require(bool(data.get("error")), f"{name}: missing load failure diagnostic")
            # Remove generated absolute staging paths from durable evidence.
            if "error" in data:
                data["error"] = data["error"].replace(str(directory), "<case-dir>")
            evidence["cases"].append({"name": name, "passed": True, "exit_code": completed.returncode,
                                      "result": data, "lifecycle_log": completed.stderr.splitlines()})
            print(f"PASS {name}: " + (f"consumer result {data['consumer_result']}" if success else
                                       "DLL loading rejected as expected"), flush=True)
    if args.external_consumer:
        with tempfile.TemporaryDirectory(prefix="sdk-consumer-", dir=output) as temporary:
            directory = Path(temporary).resolve()
            require(directory.is_relative_to(output), "SDK case directory escaped output directory")
            shutil.copy2(args.external_consumer.resolve(strict=True), directory / "Consumer.dll")
            shutil.copy2(provider, directory / "Provider.dll")
            env = dict(os.environ, QT_FORCE_STDERR_LOGGING="1")
            env["PATH"] = str(qt_bin) + os.pathsep + str(directory)
            completed = subprocess.run([str(executable), "--plugin-dir", str(directory)],
                                       env=env, cwd=output, capture_output=True,
                                       text=True, encoding="utf-8", timeout=15)
            data = json.loads(completed.stdout)
            require(completed.returncode == 0 and data.get("consumer_result") == 84,
                    f"SDK consumer failed: {completed.stderr} {completed.stdout}")
            require(not data["provider_mapped_after_metadata"] and data["provider_mapped_after_consumer"],
                    "SDK consumer has an unexpected DLL mapping lifecycle")
            require("Consumer root constructed" in completed.stderr
                    and "Provider root constructed" not in completed.stderr,
                    "SDK consumer has an unexpected plugin root lifecycle")
            evidence["cases"].append({"name": "independent-sdk-consumer", "passed": True,
                                      "exit_code": completed.returncode, "result": data,
                                      "lifecycle_log": completed.stderr.splitlines()})
            print("PASS independent-sdk-consumer: consumer result 84", flush=True)
    if args.without_api_build:
        require(args.dumpbin is not None, "The missing-export control currently requires dumpbin")
        negative = args.without_api_build.resolve(strict=True)
        result = subprocess.run([str(args.dumpbin.resolve(strict=True)), "/exports",
                                 str(negative / "plugins/Provider.dll")],
                                check=True, capture_output=True, text=True, timeout=10)
        negative_exports = re.findall(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)",
                                      result.stdout, re.MULTILINE)
        require(set(negative_exports) == {"qt_plugin_instance", "qt_plugin_query_metadata_v2"},
                f"Unexpected exports in the no-API-export control: {negative_exports}")
        log = (negative / "BuildLog.txt").read_text(encoding="utf-8-sig")
        require("LNK2019" in log and "providerAdd" in log and "multiply" in log and "LNK1120" in log,
                "Missing expected unresolved-symbol diagnostics in the negative build log")
        evidence["api_export_negative_control"] = {
            "passed": True, "exports": negative_exports,
            "diagnostics": [line for line in log.splitlines() if re.search(r"LNK(?:2019|1120)", line)]}
        print("PASS missing API exports: Qt entry points remain, consumer link fails with LNK2019", flush=True)
    (output / "Results.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"Verified exports, native imports, metadata isolation and {len(evidence['cases'])} load cases")


if __name__ == "__main__":
    main()
