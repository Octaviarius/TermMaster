"""Synthetic SDK and process-protocol acceptance; no devices or network."""
import asyncio
from enum import Enum
import json
from pathlib import Path
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "src" / "Python" / "Runtime"
sys.path.insert(0, str(RUNTIME))
from termaster import Processor, Property, Input, Output, Node, handles
from termaster.Sdk import Context, describe

SIMPLE = '''from termaster import *
class Codeflow(Processor):
    incoming = Input(bytes)
    outgoing = Output(bytes)
    @handles(incoming, output=outgoing)
    def process(self, data):
        return data
'''


class Client:
    def __init__(self):
        self.process = subprocess.Popen([sys.executable, "-u", "-I", "-B", str(RUNTIME / "CodeflowHost.py")],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.frames = queue.Queue()
        self.diagnostics = bytearray()
        def exact(size):
            data = bytearray()
            while len(data) < size:
                block = self.process.stdout.read(size - len(data))
                if not block:
                    raise EOFError()
                data.extend(block)
            return bytes(data)
        def read():
            try:
                while True:
                    size = struct.unpack(">I", exact(4))[0]
                    header = json.loads(exact(size))
                    self.frames.put((header, exact(header["bytes"])))
            except BaseException as error:
                self.frames.put(error)
        def stderr():
            while True:
                block = self.process.stderr.read(1)
                if not block:
                    return
                self.diagnostics.extend(block)
        threading.Thread(target=read, daemon=True).start()
        threading.Thread(target=stderr, daemon=True).start()

    def send(self, op, data=b"", **values):
        header = json.dumps(dict(op=op, bytes=len(data), **values)).encode()
        self.process.stdin.write(struct.pack(">I", len(header)) + header + data)
        self.process.stdin.flush()

    def receive(self, timeout=5):
        item = self.frames.get(timeout=timeout)
        if isinstance(item, BaseException):
            raise item
        assert item[0]["protocol"] == 1
        return item

    def close(self):
        if self.process.poll() is None:
            try:
                self.send("shutdown")
                self.process.wait(timeout=2)
            except (subprocess.TimeoutExpired, BrokenPipeError):
                self.process.kill()
                self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


class SdkTests(unittest.TestCase):
    def test_inheritance_types_and_schema(self):
        class Mode(Enum):
            FIXED = "fixed"
            VARIABLE = "variable"
        class Base(Processor):
            mode = Property(Mode, default=Mode.FIXED)
            path = Property(Path, default="packets.bin", mode="save")
            enabled = Property(bool, default=True)
            incoming = Input()
            outgoing = Output()
            @handles(incoming, output=outgoing)
            def run(self, data):
                return data
        class Derived(Base):
            width = Property(int, default=2, minimum=1, maximum=8, step=1, when=Base.mode.is_(Mode.FIXED))
            ratio = Property(float, default=1.5)
        description = describe(Derived)
        self.assertEqual(description["schema"]["oneOf"][0]["properties"]["width"]["minimum"], 1)
        obj = Derived(directory="/workspace")
        self.assertEqual(obj.path, Path("/workspace/packets.bin"))
        self.assertIs(obj.mode, Mode.FIXED)
        self.assertEqual(obj.ratio, 1.5)
        with self.assertRaises(AttributeError):
            obj.width = 3
        with self.assertRaises(ValueError):
            Derived(parameters={"width": 99})
        with self.assertRaises(TypeError):
            Derived(parameters={"enabled": 1})
        json.dumps(description)

    def test_invalid_shapes_conditions_and_numeric_bounds(self):
        class Empty(Processor):
            pass
        with self.assertRaises(ValueError):
            describe(Empty)
        namespace = {}
        exec(SIMPLE, namespace)
        Base = namespace["Codeflow"]
        class Bad(Base):
            value = Property(int, default=2 ** 54)
        with self.assertRaises(ValueError):
            describe(Bad)
        class Unsupported(Base):
            selector = Property(int, default=1, choices=[1, 2])
            value = Property(int, default=1, when=selector.is_(1))
        with self.assertRaises(ValueError):
            describe(Unsupported)


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.client = Client()

    def tearDown(self):
        self.client.close()

    def create(self, code=SIMPLE, node="a", **values):
        self.client.send("create", request=1, node=node, source={"code": code}, **values)
        frame, _ = self.client.receive()
        self.assertEqual(frame["op"], "ready", frame)

    def packet(self, data, node="a", request=2, port=0):
        self.client.send("packet", data, request=request, node=node, port=port)
        packets = []
        while True:
            frame, payload = self.client.receive()
            if frame["op"] == "done":
                return packets, frame
            self.assertEqual(frame["op"], "output", frame)
            packets.append((frame["port"], payload))

    def test_binary_empty_many_and_independent_instances(self):
        code = SIMPLE.replace("return data", "self.count = getattr(self, 'count', 0) + 1\n        return [b'', data, bytes([self.count])]")
        self.create(code)
        self.create(code, node="b")
        self.assertEqual(self.packet(b"\0\xff")[0], [(0, b""), (0, b"\0\xff"), (0, b"\1")])
        self.assertEqual(self.packet(b"x", request=3)[0][-1], (0, b"\2"))
        self.assertEqual(self.packet(b"x", node="b")[0][-1], (0, b"\1"))

    def test_describe_does_not_construct_and_missing_file_snapshot(self):
        code = SIMPLE.replace("    incoming", "    def __init__(self):\n        raise RuntimeError('constructed')\n    incoming")
        self.client.send("describe", request=1, source={"code": code, "file": "missing/main.py"})
        frame, _ = self.client.receive()
        self.assertEqual(frame["op"], "described", frame)
        self.assertEqual(frame["description"]["className"], "Codeflow")

    def test_native_stdout_and_stdin_isolation_modules(self):
        code = "import os, sys, zlib, sqlite3\nprint('diagnostic')\nos.write(1,b'native output')\nassert sys.stdin.read() == ''\n" + SIMPLE
        self.create(code)
        self.assertEqual(self.packet(b"ok")[0], [(0, b"ok")])

    def test_metadata_drift_and_custom_constructor_rejection(self):
        self.client.send("create", request=1, node="a", source={"code": SIMPLE}, expected={"title": "changed"})
        self.assertIn("descriptor changed", self.client.receive()[0]["error"])
        code = SIMPLE.replace("    incoming", "    def __init__(self): pass\n    incoming")
        self.client.send("create", request=2, node="a", source={"code": code})
        self.assertIn("initialize state in start", self.client.receive()[0]["error"])

    def test_explicit_child_output_ports(self):
        code = SIMPLE.replace("    incoming", "    child = Node('native.paired')\n    incoming").replace("def process", "async def process").replace("return data", "results = await self.child.process(data, port=1, with_ports=True)\n        return [bytes([p.port]) + p.data for p in results]")
        self.create(code)
        self.client.send("packet", b"a", node="a", request=2, port=0)
        call, _ = self.client.receive()
        self.assertEqual(call["port"], 1)
        self.client.send("call_output", b"z", node="a", request=call["request"], port=1)
        self.client.send("call_done", node="a", request=call["request"], error="")
        self.assertEqual(self.client.receive()[1], b"\1z")
        self.assertEqual(self.client.receive()[0]["op"], "done")

    def test_private_relative_imports(self):
        with tempfile.TemporaryDirectory() as root:
            for i in (1, 2):
                directory = Path(root) / str(i)
                directory.mkdir()
                (directory / "helpers.py").write_text(f"suffix = {bytes([i])!r}\n", encoding="utf-8")
                code = "from .helpers import suffix\n" + SIMPLE.replace("return data", "return data + suffix")
                self.client.send("create", request=i, node=str(i), source={"code": code, "file": str(directory / "main.py")})
                frame, _ = self.client.receive()
                self.assertEqual(frame["op"], "ready", frame)
            self.assertEqual(self.packet(b"", node="1")[0], [(0, b"\1")])
            self.assertEqual(self.packet(b"", node="2")[0], [(0, b"\2")])

    def test_mixed_output_and_quota_fail_atomically(self):
        for i, body in enumerate(("self.outgoing.emit(data)\n        return data", "return [data, data, data]", "return 'bad'")):
            self.create(SIMPLE.replace("return data", body), node=str(i), limits={"maxBytes": 4, "maxChunks": 2})
            packets, frame = self.packet(b"xx", node=str(i))
            self.assertEqual(packets, [])
            self.assertTrue(frame["error"])

    def test_async_native_child_rpc(self):
        code = SIMPLE.replace("    incoming", "    child = Node('native.fixture', option=3)\n    incoming").replace("def process", "async def process").replace("return data", "return await self.child.process(data)")
        self.create(code)
        self.client.send("packet", b"\0", node="a", request=2, port=0)
        call, data = self.client.receive()
        self.assertEqual(call["op"], "call")
        self.assertEqual(call["alias"], "child")
        self.assertEqual(call["configuration"], {"option": 3})
        self.assertEqual(data, b"\0")
        for payload in (b"", b"result\0"):
            self.client.send("call_output", payload, node="a", request=call["request"], port=0)
        self.client.send("call_done", node="a", request=call["request"], error="")
        self.assertEqual(self.client.receive()[1], b"")
        self.assertEqual(self.client.receive()[1], b"result\0")
        self.assertEqual(self.client.receive()[0]["error"], "")

    def test_startup_native_rpc_keeps_control_reader_running(self):
        code = SIMPLE.replace("    incoming", "    child = Node('native.fixture')\n    async def start(self):\n        self.seed = b''.join(await self.child.process(b'seed'))\n    incoming").replace("return data", "return self.seed + data")
        self.client.send("create", request=1, node="a", source={"code": code})
        call, data = self.client.receive()
        self.assertEqual((call["op"], data), ("call", b"seed"))
        self.client.send("call_output", b"started:", node="a", request=call["request"], port=0)
        self.client.send("call_done", node="a", request=call["request"], error="")
        self.assertEqual(self.client.receive()[0]["op"], "ready")
        self.assertEqual(self.packet(b"packet")[0], [(0, b"started:packet")])

    def test_cancel_pending_start_joins_owned_tasks_and_stop(self):
        code = "import asyncio\n" + SIMPLE.replace("    incoming", "    child = Node('native.fixture')\n    async def owned(self):\n        try:\n            await asyncio.sleep(60)\n        finally:\n            self.ctx.log('owned task stopped')\n    async def start(self):\n        self.ctx.create_task(self.owned())\n        await asyncio.sleep(0)\n        await self.child.process(b'seed')\n    async def stop(self):\n        await asyncio.sleep(0.01)\n        self.ctx.log('stop hook completed')\n    incoming")
        self.client.send("create", request=1, node="a", source={"code": code})
        call, _ = self.client.receive()
        self.assertEqual(call["op"], "call")
        self.client.send("create", request=2, node="a", source={"code": code})
        duplicate, _ = self.client.receive()
        self.assertEqual(duplicate["op"], "error")
        self.client.send("cancel", request=3, node="a")
        events = []
        while True:
            event, _ = self.client.receive()
            self.assertNotEqual(event["op"], "ready")
            events.append(event)
            if event["op"] == "cancelled":
                break
        self.assertEqual([event["message"] for event in events if event["op"] == "log"],
                         ["owned task stopped", "stop hook completed"])
        self.assertEqual(events[-1]["request"], 3)
        # Stale child completion must not resurrect the cancelled source revision.
        self.client.send("call_done", node="a", request=call["request"], error="")
        self.create(node="b")
        self.assertEqual(self.packet(b"alive", node="b")[0], [(0, b"alive")])

    def test_paired_ports_sorted_by_flow(self):
        code = '''from termaster import *
class Codeflow(Processor):
    tx = Input(flow='tx')
    txout = Output(flow='tx')
    rx = Input(flow='rx')
    rxout = Output(flow='rx')
    @handles(tx, output=txout)
    def send(self, data): return b'tx' + data
    @handles(rx, output=rxout)
    def receive(self, data): return b'rx' + data
'''
        self.create(code)
        self.assertEqual(self.packet(b"x", port=0)[0], [(0, b"rxx")])
        self.assertEqual(self.packet(b"x", port=1)[0], [(1, b"txx")])

    def test_async_cancel_stop_and_timer(self):
        code = "import asyncio\n" + SIMPLE.replace("    incoming", "    async def start(self):\n        self.ctx.timer(0.01, lambda: self.outgoing.emit(b'timer'))\n    def stop(self):\n        self.ctx.log('stopped')\n    incoming").replace("def process", "async def process").replace("return data", "await asyncio.sleep(60)")
        self.create(code)
        frame, data = self.client.receive()
        self.assertEqual((frame["op"], frame["request"], data), ("output", 0, b"timer"))
        self.client.send("packet", b"", node="a", request=2, port=0)
        self.client.send("cancel", node="a", request=3)
        frame, _ = self.client.receive()
        self.assertEqual(frame["op"], "log")
        self.assertEqual(self.client.receive()[0]["op"], "cancelled")

    def test_import_traceback_and_infinite_loop_parent_termination(self):
        self.client.send("describe", request=1, source={"code": "raise ValueError('fixture failure')", "file": "fixture.py"})
        frame, _ = self.client.receive()
        self.assertIn("fixture.py", frame["error"])
        self.create(SIMPLE.replace("return data", "while True: pass"))
        self.client.send("packet", b"", node="a", request=2, port=0)
        with self.assertRaises(queue.Empty):
            self.client.receive(timeout=0.15)
        self.client.process.kill()
        self.client.process.wait(timeout=2)

    def test_blocked_native_extension_parent_termination(self):
        # time.sleep releases the GIL but blocks the host event-loop thread.
        code = "import time\n" + SIMPLE.replace("return data", "time.sleep(60)\n        return data")
        self.create(code)
        self.client.send("packet", b"", node="a", request=2, port=0)
        with self.assertRaises(queue.Empty):
            self.client.receive(timeout=0.15)
        self.client.send("cancel", node="a", request=3)
        with self.assertRaises(queue.Empty):
            self.client.receive(timeout=0.15)
        self.client.process.kill()
        self.client.process.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
