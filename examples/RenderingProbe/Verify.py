"""Capture all renderers at several DPIs and compare the raster paths (Pillow required)."""

import argparse
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, ImageStat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--qt-bin", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    results = []
    for scale in ["1", "1.5", "2"]:
        env = dict(os.environ, QT_SCALE_FACTOR=scale)
        env["PATH"] = str(args.qt_bin.resolve(strict=True)) + os.pathsep + env["PATH"]
        for method in range(1, 6):
            destination = output / f"Snapshot-{scale}-{method}.png"
            completed = subprocess.run(
                [str(executable), "--method", str(method), "--cols", "100", "--rows", "22",
                 "--snapshot", str(destination)], env=env, timeout=20, capture_output=True, text=True)
            if completed.returncode:
                raise RuntimeError(f"scale {scale}, method {method}: {completed.stderr}")
        baseline = Image.open(output / f"Snapshot-{scale}-1.png").convert("RGB")
        for method in range(2, 6):
            image = Image.open(output / f"Snapshot-{scale}-{method}.png").convert("RGB")
            if image.size != baseline.size:
                raise RuntimeError(f"scale {scale}, method {method}: geometry mismatch")
            difference = ImageChops.difference(baseline, image)
            mean = ImageStat.Stat(difference).mean
            results.append({"scale": scale, "method": method, "size": image.size,
                            "mean_channel_difference": mean,
                            "difference_bounds": difference.getbbox()})
            # Raster paths should reproduce the same image, allowing small
            # font-engine/cache rasterization variation at larger scale factors.
            # GPU antialiasing differs and is reviewed visually, not accepted by this threshold.
            if method < 5 and max(mean) > 0.15:
                raise RuntimeError(f"scale {scale}, method {method}: raster difference {mean}")
            print(f"scale {scale}, method {method}: mean channel difference {mean}", flush=True)
    (output / "VisualChecks.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
