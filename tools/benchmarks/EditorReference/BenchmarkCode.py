"""Measure an isolated installed VS Code through CDP and native window sizing.

CDP renderer TaskDuration is reported separately from command/two-frame wall
latency. DOM counts and token colors prove viewport virtualization and highlighting.
Requires the already-installed websocket-client package; downloads nothing.
"""
import argparse
from collections import defaultdict
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
import urllib.request

import websocket
from BenchmarkNotepad import fixture, message, paint, user, windows


class Cdp:
    def __init__(self, target):
        self.ws = websocket.create_connection(target["webSocketDebuggerUrl"], timeout=20, suppress_origin=True)
        self.sequence = 0

    def call(self, method, params=None):
        self.sequence += 1
        self.ws.send(json.dumps({"id": self.sequence, "method": method, "params": params or {}}))
        while True:
            response = json.loads(self.ws.recv())
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response.get("result", {})

    def evaluate(self, expression):
        result = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in result:
            raise RuntimeError(result["exceptionDetails"])
        return result["result"].get("value")

    def settle(self):
        self.evaluate("new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(true))))")

    def metrics(self):
        return {m["name"]: m["value"] for m in self.call("Performance.getMetrics")["metrics"]}

    def snapshot(self):
        return self.evaluate(r"""(() => {
            const view = document.querySelector('.view-lines');
            const lines = [...(view?.querySelectorAll('.view-line') ?? [])];
            const text = lines.map(e => e.textContent.replace(/\u00a0/g,' ')).join('\n');
            const ids = [...text.matchAll(/"index":\s*(\d+)/g)].map(x => Number(x[1]));
            return {title:document.title,width:innerWidth,height:innerHeight,
                renderedLines:lines.length,firstId:ids[0] ?? null,lastId:ids.at(-1) ?? null,
                colors:[...new Set(lines.flatMap(e=>[...e.querySelectorAll('span')].map(s=>getComputedStyle(s).color)))],
                activeClass:document.activeElement?.className,
                font:getComputedStyle(view ?? document.body).fontFamily};
        })()""")

    def key(self, key, virtual_key, control=False):
        params = {"key": key, "code": key, "windowsVirtualKeyCode": virtual_key, "modifiers": 2 if control else 0}
        self.call("Input.dispatchKeyEvent", {"type": "keyDown", **params})
        self.call("Input.dispatchKeyEvent", {"type": "keyUp", **params})

    def highlighted(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            state = self.snapshot()
            if len(state["colors"]) >= 3:
                return state
            self.settle()
        raise RuntimeError("Visible syntax highlighting did not become ready within 30 seconds")


def run(args, count, round_):
    directory = args.scratch / f"{count}-Round{round_}"
    (directory / "Profile/User").mkdir(parents=True)
    file = directory / f"VsFixture{count}Round{round_}.json"
    fixture(file, count, True)
    original = hashlib.sha256(file.read_bytes()).hexdigest()
    settings = {"editor.fontFamily": "Courier New", "editor.fontSize": 16, "editor.wordWrap": "off",
                "editor.smoothScrolling": False, "editor.scrollBeyondLastLine": False,
                "editor.minimap.enabled": True, "workbench.startupEditor": "none", "window.restoreWindows": "none"}
    (directory / "Profile/User/settings.json").write_text(json.dumps(settings), encoding="utf-8")
    log = (directory / "Code.log").open("w", encoding="utf-8")
    process = subprocess.Popen([str(args.exe), "--user-data-dir", str(directory / "Profile"),
                                "--extensions-dir", str(directory / "Extensions"), "--disable-extensions",
                                "--new-window", "--skip-welcome", "--skip-release-notes",
                                f"--remote-debugging-port={args.port}", str(file)], stdout=log, stderr=log)
    cdp = None
    hwnd = None
    try:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{args.port}/json/list", timeout=1))
                pages = [t for t in targets if t["type"] == "page" and "workbench" in t["url"]]
                if len(pages) == 1:
                    cdp = Cdp(pages[0])
                    state = cdp.snapshot()
                    if file.name in state["title"] and state["renderedLines"] > 0 and len(state["colors"]) >= 3:
                        break
                    cdp.ws.close()
                    cdp = None
            except (OSError, RuntimeError, websocket.WebSocketException):
                pass
            time.sleep(.2)
        if not cdp:
            raise RuntimeError("No isolated highlighted VS Code editor; inspect Code.log")
        user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        matching = []
        for candidate, cls in windows():
            title = c.create_unicode_buffer(2048)
            user.GetWindowTextW(candidate, title, len(title))
            if file.name in title.value and cls == "Chrome_WidgetWin_1":
                matching.append(candidate)
        if len(matching) != 1:
            raise RuntimeError("Expected exactly one window for this unique generated file")
        hwnd = matching[0]
        if not user.IsWindowEnabled(hwnd):
            raise RuntimeError("Owned editor is blocked by a modal")
        user.ShowWindow(hwnd, 9)
        user.SetWindowPos(hwnd, None, 60, 60, 1160, 790, 0x4)
        user.SetForegroundWindow(hwnd)
        cdp.settle()
        time.sleep(1)
        cdp.call("Performance.enable", {"timeDomain": "threadTicks"})
        version = cdp.call("Browser.getVersion")
        cdp.key("End", 35, True)
        cdp.settle()
        bottom = cdp.highlighted()
        assert bottom["lastId"] == count - 1, bottom
        assert len(bottom["colors"]) >= 3 and bottom["renderedLines"] < 200, bottom
        cdp.key("Home", 36, True)
        cdp.settle()
        loaded = cdp.highlighted()
        assert loaded["firstId"] == 0 and len(loaded["colors"]) >= 3, loaded
        samples = []
        def measure(stage, action):
            before = cdp.metrics()
            start = time.perf_counter_ns()
            action()
            cdp.settle()
            state = cdp.highlighted()
            elapsed = (time.perf_counter_ns() - start) / 1e6
            after = cdp.metrics()
            assert 0 < state["renderedLines"] < 200
            record = {"stage": stage, "readyWallMs": elapsed, "renderedLines": state["renderedLines"],
                      "firstId": state["firstId"], "lastId": state["lastId"], "tokenColors": len(state["colors"])}
            for metric in ("TaskDuration", "ScriptDuration", "LayoutDuration", "RecalcStyleDuration"):
                record[metric + "Ms"] = (after[metric] - before[metric]) * 1000
            samples.append(record)
        def resize(width, height):
            if not user.SetWindowPos(hwnd, None, 0, 0, width, height, 0x4 | 0x2 | 0x10):
                raise RuntimeError("SetWindowPos failed")
        for phase in ("width", "height", "diagonal"):
            resize(1160, 790)
            cdp.settle()
            for i in range(30):
                target = (1161 + i, 790) if phase == "width" else (1160, 791 + i) if phase == "height" else (
                    (1320, 880) if i % 2 == 0 else (1160, 790))
                measure("resize." + phase, lambda: resize(*target))
        resize(1160, 790)
        cdp.settle()
        cdp.key("Home", 36, True)
        cdp.settle()
        previous_top = 0
        for i in range(30):
            measure("scroll.page", lambda: cdp.key("PageDown", 34))
            assert samples[-1]["firstId"] is not None and samples[-1]["firstId"] > previous_top
            previous_top = samples[-1]["firstId"]
        for i in range(20):
            before = cdp.snapshot()
            measure("scroll.jump", lambda: cdp.key("Home", 36, True) if i % 2 else cdp.key("End", 35, True))
            after = cdp.snapshot()
            assert after["firstId"] != before["firstId"]
            assert (after["lastId"] == count - 1) if i % 2 == 0 else (after["firstId"] == 0)
        assert hashlib.sha256(file.read_bytes()).hexdigest() == original
        return {"editor": "VS Code", "rows": count, "round": round_, "browser": version,
                "metricTimeDomain": "threadTicks",
                "sha256": original, "fileBytes": file.stat().st_size, "dpi": user.GetDpiForWindow(hwnd),
                "loaded": loaded, "bottom": bottom, "samples": samples, "ok": True}
    finally:
        if cdp:
            cdp.ws.close()
        if hwnd:
            message(hwnd, 0x10)
        if process.poll() is None:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)
        log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path(os.environ["LOCALAPPDATA"]) / "Programs/Microsoft VS Code/Code.exe")
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=19338)
    parser.add_argument("--rounds", type=int, default=2)
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results = []
    for count in (10000, 100000):
        for round_ in range(1, args.rounds + 1):
            print(f"VS Code {count} highlighted rows round={round_}", flush=True)
            results.append(run(args, count, round_))
            args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    grouped = defaultdict(list)
    for result in results:
        for sample in result["samples"]:
            grouped[(result["rows"], sample["stage"])].append(sample)
    summary = []
    for (count, stage), values in grouped.items():
        item = {"rows": count, "stage": stage, "samples": len(values)}
        for key in ("readyWallMs", "TaskDurationMs", "LayoutDurationMs", "RecalcStyleDurationMs"):
            data = sorted(v[key] for v in values)
            item[key + "Median"] = statistics.median(data)
            item[key + "P95"] = data[int(len(data) * .95 - .000001)]
        summary.append(item)
        print(json.dumps(item), flush=True)
    args.output.with_name("CodeSummary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
