"""Benchmark an installed Notepad++ through its native Scintilla/Win32 APIs.

Only fresh, isolated processes and generated files are used. No simulated typing
or remote-control round trips are included. Timings stop after invalid native
windows are painted, not after DWM/monitor presentation.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time


user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenThread.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.GetThreadTimes.argtypes = [w.HANDLE] + [c.POINTER(c.c_ulonglong)] * 4
enum_callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [enum_callback, w.LPARAM]
user.EnumChildWindows.argtypes = [w.HWND, enum_callback, w.LPARAM]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]
user.ShowWindow.argtypes = [w.HWND, c.c_int]
user.SetForegroundWindow.argtypes = [w.HWND]
user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
user.RedrawWindow.argtypes = [w.HWND, c.c_void_p, c.c_void_p, w.UINT]
user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user.GetDpiForWindow.argtypes = [w.HWND]
user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
user.SendMessageTimeoutW.restype = c.c_size_t
try:
    user.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]
    user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
except AttributeError:
    pass


def message(hwnd, code, first=0, second=0):
    result = c.c_size_t()
    if not user.SendMessageTimeoutW(hwnd, code, first, second, 2, 10000, c.byref(result)):
        raise RuntimeError(f"Native message {code} failed or timed out: {c.get_last_error()}")
    return result.value


def windows(pid=None, parent=None):
    found = []
    @enum_callback
    def visit(hwnd, _):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if user.IsWindowVisible(hwnd) and (pid is None or owner.value == pid):
            name = c.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, len(name))
            found.append((hwnd, name.value))
        return True
    if parent:
        user.EnumChildWindows(parent, visit, 0)
    else:
        user.EnumWindows(visit, 0)
    return found


def paint(hwnd):
    # Deliver pending paints to the window and children; do not invalidate them
    # or defeat Scintilla's native scroll blits by forcing a full repaint.
    if not user.RedrawWindow(hwnd, None, None, 0x100 | 0x80):
        raise RuntimeError("RedrawWindow failed")


def size(hwnd):
    rect = w.RECT()
    if not user.GetClientRect(hwnd, c.byref(rect)):
        raise RuntimeError("GetClientRect failed")
    return [rect.right, rect.bottom]


def cpu_ms(thread):
    created = c.c_ulonglong()
    exited = c.c_ulonglong()
    system = c.c_ulonglong()
    user_time = c.c_ulonglong()
    if not kernel.GetThreadTimes(thread, c.byref(created), c.byref(exited), c.byref(system), c.byref(user_time)):
        raise RuntimeError("GetThreadTimes failed")
    return (system.value + user_time.value) / 10000


def fixture(path, count, colored):
    with path.open("w", encoding="utf-8", newline="\n") as f:
        if colored:
            f.write("[\n")
        for i in range(count):
            if colored:
                f.write(json.dumps({"index": i, "enabled": bool(i % 2),
                                    "text": "Русский 日本語 é 😀 " + chr(97 + i % 26) * 28}, ensure_ascii=False))
                f.write(("," if i + 1 < count else "") + "\n")
            else:
                f.write(f"{i:06d} | plain ASCII text " + chr(97 + i % 26) * 28 + "\n")
        if colored:
            f.write("]")


def run(exe, root, count, colored, round_):
    kind = "json-unicode" if colored else "ascii"
    directory = root / f"{count}-{kind}-Round{round_}"
    directory.mkdir()
    (directory / "Settings").mkdir()
    path = directory / ("Fixture.json" if colored else "Fixture.txt")
    fixture(path, count, colored)
    original = hashlib.sha256(path.read_bytes()).hexdigest()
    process = subprocess.Popen([str(exe), "-multiInst", "-nosession", "-noPlugin", "-ro",
                                "-settingsDir=" + str(directory / "Settings"), str(path)])
    hwnd = None
    thread = None
    try:
        deadline = time.monotonic() + 30
        scintilla = None
        while time.monotonic() < deadline:
            for candidate, _ in windows(process.pid):
                children = [h for h, cls in windows(process.pid, candidate) if cls == "Scintilla"]
                if children:
                    hwnd = candidate
                    scintilla = children[0]
                    break
            if scintilla and message(scintilla, 2006) == path.stat().st_size:
                break
            if process.poll() is not None:
                raise RuntimeError(f"Notepad++ exited early: {process.returncode}")
            time.sleep(.05)
        if not hwnd or not scintilla or not user.IsWindowEnabled(hwnd):
            raise RuntimeError("No enabled owned Notepad++ editor window")
        owner = w.DWORD()
        thread_id = user.GetWindowThreadProcessId(hwnd, c.byref(owner))
        assert owner.value == process.pid
        thread = kernel.OpenThread(0x0800, False, thread_id)
        if not thread:
            raise RuntimeError("Cannot query owned GUI thread")
        message(scintilla, 2171, 1)  # Read-only.
        message(scintilla, 2268, 0)  # No wrapping, as in the terminal experiment.
        for style in range(256):
            message(scintilla, 2055, style, 12)  # 12 pt; preserve syntax colors.
        user.ShowWindow(hwnd, 9)
        user.SetWindowPos(hwnd, None, 60, 60, 1160, 790, 0x4)
        user.SetForegroundWindow(hwnd)
        paint(hwnd)
        time.sleep(.3)
        lines = message(scintilla, 2154)
        expected_lines = count + (2 if colored else 1)
        assert lines == expected_lines, (lines, expected_lines)
        assert message(scintilla, 2269) == 0
        assert message(scintilla, 2159) == 0
        styles = sorted({message(scintilla, 2010, i) for i in range(min(512, path.stat().st_size))})
        assert not colored or len(styles) >= 3, styles
        samples = []
        def measure(stage, action):
            before_cpu = cpu_ms(thread)
            start = time.perf_counter_ns()
            action()
            paint(hwnd)
            elapsed = (time.perf_counter_ns() - start) / 1e6
            samples.append({"stage": stage, "wallMs": elapsed, "guiCpuMs": cpu_ms(thread) - before_cpu})
        def resize(width, height):
            if not user.SetWindowPos(hwnd, None, 0, 0, width, height, 0x4 | 0x2 | 0x10):
                raise RuntimeError("SetWindowPos failed")
        for phase in ("width", "height", "diagonal"):
            resize(1160, 790)
            paint(hwnd)
            for i in range(30):
                target = (1161 + i, 790) if phase == "width" else (1160, 791 + i) if phase == "height" else (
                    (1320, 880) if i % 2 == 0 else (1160, 790))
                measure("resize." + phase, lambda: resize(*target))
        resize(1160, 790)
        paint(hwnd)
        def scroll(stage, row):
            measure(stage, lambda: message(scintilla, 2613, row))
            assert message(scintilla, 2152) == row
            samples[-1]["topRow"] = row
        message(scintilla, 2613, 3000)
        paint(hwnd)
        for i in range(1, 31): scroll("scroll.line", 3000 + i)
        page = message(scintilla, 2370)
        message(scintilla, 2613, 2000)
        paint(hwnd)
        for i in range(1, 21): scroll("scroll.page", 2000 + i * page)
        for i in range(30): scroll("scroll.jump", 50 + (i * 7919) % (count - 100))
        assert message(scintilla, 2154) == lines
        assert message(scintilla, 2159) == 0
        assert hashlib.sha256(path.read_bytes()).hexdigest() == original
        return {"editor": "Notepad++", "rows": count, "content": kind, "round": round_,
                "documentLines": lines, "fileBytes": path.stat().st_size, "sha256": original,
                "styleIds": styles, "lexer": message(scintilla, 4002),
                "technology": message(scintilla, 2631), "layoutCache": message(scintilla, 2273),
                "fontPoints": message(scintilla, 2485, 32), "dpi": user.GetDpiForWindow(hwnd),
                "editorViewport": size(scintilla), "pageLines": page, "samples": samples, "ok": True}
    finally:
        if thread:
            kernel.CloseHandle(thread)
        if hwnd and process.poll() is None:
            message(hwnd, 0x10)  # Normal close of this owned, unmodified instance only.
            process.wait(timeout=15)
        elif process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("C:/Program Files/Notepad++/notepad++.exe"))
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=2)
    args = parser.parse_args()
    assert args.exe.is_file()
    args.scratch.mkdir(parents=True, exist_ok=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results = []
    for count in (10000, 100000):
        for colored in (False, True):
            for round_ in range(1, args.rounds + 1):
                print(f"Notepad++ {count} rows colored={colored} round={round_}", flush=True)
                results.append(run(args.exe, args.scratch, count, colored, round_))
                args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    grouped = defaultdict(list)
    for result in results:
        for sample in result["samples"]:
            grouped[(result["rows"], result["content"], sample["stage"])].append(sample)
    summary = []
    for (count, kind, stage), samples in grouped.items():
        values = [sample["wallMs"] for sample in samples]
        item = {"rows": count, "content": kind, "stage": stage, "samples": len(values),
                "medianMs": statistics.median(values), "p95Ms": sorted(values)[int(len(values) * .95 - .000001)],
                "maximumMs": max(values), "meanGuiCpuMs": sum(sample["guiCpuMs"] for sample in samples) / len(samples)}
        summary.append(item)
        print(json.dumps(item), flush=True)
    args.output.with_name("Summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
