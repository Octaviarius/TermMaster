"""Private binary Codeflow execution endpoint. Not a sandbox."""
import asyncio
import hashlib
import inspect
import json
import os
from pathlib import Path
import struct
import sys
import threading
import traceback
import types

# Capture control descriptors before importing or executing any supplied source.
_writer = os.fdopen(os.dup(1), "wb", buffering=0)
_reader = os.fdopen(os.dup(0), "rb", buffering=0)
os.dup2(2, 1)
sys.stdout = sys.stderr
_null = os.open(os.devnull, os.O_RDONLY)
os.dup2(_null, 0)
os.close(_null)
sys.stdin = open(os.devnull, "r", encoding="utf-8")
if os.name == "nt":
    import msvcrt
    msvcrt.setmode(_writer.fileno(), os.O_BINARY)
    msvcrt.setmode(_reader.fileno(), os.O_BINARY)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from termaster import Processor
from termaster.Sdk import ChildPacket, Context, Input, Output, describe, descriptors

_HEADER_LIMIT = 4 * 1024 * 1024
_PAYLOAD_LIMIT = 64 * 1024 * 1024


def send(header, data=b""):
    header = dict(header, bytes=len(data), protocol=1)
    encoded = json.dumps(header, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) > _HEADER_LIMIT:
        raise ValueError("protocol header too large")
    frame = memoryview(struct.pack(">I", len(encoded)) + encoded + data)
    while frame:
        written = _writer.write(frame)
        if not written:
            raise BrokenPipeError("protocol output closed")
        frame = frame[written:]


def read_exact(size):
    parts = bytearray()
    while len(parts) < size:
        part = _reader.read(size - len(parts))
        if not part:
            raise EOFError("protocol input closed")
        parts.extend(part)
    return bytes(parts)


def read_frame():
    size = struct.unpack(">I", read_exact(4))[0]
    if not 0 < size <= _HEADER_LIMIT:
        raise ValueError("invalid protocol header size")
    header = json.loads(read_exact(size).decode("utf-8"))
    size = header.get("bytes", 0)
    if type(size) is not int or not 0 <= size <= _PAYLOAD_LIMIT:
        raise ValueError("invalid protocol payload size")
    return header, read_exact(size)


def error_text():
    return traceback.format_exc()[-32768:]


def load(source):
    filename = source.get("file") or "<Codeflow>"
    code = source.get("code")
    if code is None:
        code = Path(filename).read_text(encoding="utf-8")
    if not isinstance(code, str) or len(code.encode("utf-8")) > 2 * 1024 * 1024:
        raise ValueError("source exceeds 2 MiB")
    directory = str(Path(filename).absolute().parent)
    digest = hashlib.sha256((filename + "\0" + code).encode("utf-8")).hexdigest()
    package_name = "_termaster_" + digest
    module_name = package_name + ".main"
    module = sys.modules.get(module_name)
    if module is None:
        package = types.ModuleType(package_name)
        package.__path__ = [directory]
        package.__package__ = package_name
        sys.modules[package_name] = package
        module = types.ModuleType(module_name)
        module.__file__ = filename
        module.__package__ = package_name
        sys.modules[module_name] = module
        try:
            exec(compile(code, filename, "exec"), module.__dict__)
        except BaseException:
            for name in list(sys.modules):
                if name == package_name or name.startswith(package_name + "."):
                    del sys.modules[name]
            raise
    name = source.get("className")
    if name:
        cls = getattr(module, name)
        if not inspect.isclass(cls) or not issubclass(cls, Processor) or cls is Processor or inspect.isabstract(cls):
            raise TypeError("className must select a concrete Processor subclass")
    else:
        classes = [value for value in vars(module).values() if inspect.isclass(value) and value is not Processor
                   and issubclass(value, Processor) and value.__module__ == module_name and not inspect.isabstract(value)]
        classes = list(dict.fromkeys(classes))
        classes = [value for value in classes if not any(other is not value and issubclass(other, value) for other in classes)]
        if len(classes) != 1:
            raise ValueError("source must define one concrete Processor or specify className")
        cls = classes[0]
    return cls


async def maybe(value):
    if inspect.isawaitable(value):
        return await value
    return value


class Host:
    def __init__(self):
        self.nodes = {}
        self.tasks = {}
        self.calls = {}
        self.starting = {}
        self.stopping = {}
        self.controls = set()
        self.next_call = 1000000000
        self.queue = asyncio.Queue(maxsize=256)
        self.exit = False

    async def child(self, node_id, child, data, port, limits):
        self.next_call += 1
        request = self.next_call
        future = asyncio.get_running_loop().create_future()
        state = {"future": future, "packets": [], "bytes": 0, "limits": limits}
        self.calls[(node_id, request)] = state
        send({"op": "call", "node": node_id, "request": request, "alias": child.name,
              "port": port, "configuration": child.configuration}, data)
        try:
            return await future
        finally:
            self.calls.pop((node_id, request), None)

    async def create(self, header):
        node_id = header["node"]
        if node_id in self.nodes:
            raise ValueError("node already exists")
        cls = load(header["source"])
        description = describe(cls)
        expected = header.get("expected")
        if expected is not None:
            if not isinstance(expected, dict) or any(description.get(key) != value for key, value in expected.items()):
                raise ValueError("Codeflow descriptor changed since approval; check and republish the source")
        if cls.__init__ is not Processor.__init__:
            raise TypeError("custom Processor.__init__ is unsupported; initialize state in start()")
        limits = dict(maxBytes=1024 * 1024, maxChunks=1024)
        limits.update(header.get("limits", {}))
        outputs = list(descriptors(cls, Output).values())
        context = Context(lambda output, data, request: send({"op": "output", "node": node_id, "request": request, "port": outputs.index(output)}, data),
                          lambda child, data, port: self.child(node_id, child, data, port, limits),
                          lambda level, message: send({"op": "log", "node": node_id, "request": 0, "level": level, "message": message}), limits)
        # Bind infrastructure before the optional start hook.
        instance = cls.__new__(cls)
        Processor.__init__(instance, parameters=header.get("parameters", {}), directory=header.get("directory", "."), context=context)
        state = {"instance": instance, "locks": [asyncio.Lock() for _ in description["inputs"]], "limits": limits, "stopping": False, "ready": False}
        self.nodes[node_id] = state
        try:
            await maybe(instance.start())
        except BaseException:
            # Cancellation owns cleanup after it has joined this start task.
            if not state["stopping"]:
                await context.close()
                try:
                    await maybe(instance.stop())
                finally:
                    self.nodes.pop(node_id, None)
            raise
        if not state["stopping"]:
            state["ready"] = True
            send({"op": "ready", "node": node_id, "request": header["request"]})

    async def packet(self, header, data):
        node_id = header["node"]
        request = header["request"]
        error = ""
        try:
            state = self.nodes[node_id]
            port = header["port"]
            if state["stopping"]:
                raise RuntimeError("node is stopping")
            if not state["ready"]:
                raise RuntimeError("node startup has not completed")
            if len(data) > state["limits"]["maxBytes"]:
                raise ValueError("input packet exceeds byte limit")
            if type(port) is not int or not 0 <= port < len(state["locks"]):
                raise ValueError("invalid input port")
            async with state["locks"][port]:
                await state["instance"]._invoke(port, data, request)
        except asyncio.CancelledError:
            return
        except BaseException:
            error = error_text()
        send({"op": "done", "node": node_id, "request": request, "error": error})

    async def cancel(self, node_id, request):
        previous = self.stopping.get(node_id)
        if previous is not None:
            await asyncio.shield(previous)
            send({"op": "cancelled", "node": node_id, "request": request})
            return
        self.stopping[node_id] = asyncio.current_task()
        try:
            await self._stop(node_id)
        finally:
            self.stopping.pop(node_id, None)
        send({"op": "cancelled", "node": node_id, "request": request})

    async def _stop(self, node_id):
        state = self.nodes.get(node_id)
        if state:
            state["stopping"] = True
        startup = self.starting.get(node_id)
        if startup is not None and startup is not asyncio.current_task():
            startup.cancel()
            await asyncio.gather(startup, return_exceptions=True)
        state = self.nodes.get(node_id)
        if state:
            pending = [task for (node, _), task in self.tasks.items() if node == node_id]
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
            await state["instance"].ctx.close()
            try:
                await maybe(state["instance"].stop())
            finally:
                self.nodes.pop(node_id, None)

    def control(self, coroutine, header):
        if len(self.controls) >= 256:
            coroutine.close()
            raise ValueError("pending lifecycle operation quota exceeded")
        async def execute():
            try:
                await coroutine
            except asyncio.CancelledError:
                pass
            except BaseException:
                send({"op": "error", "request": header.get("request", 0), "node": header.get("node", ""), "error": error_text()})
        task = asyncio.create_task(execute())
        self.controls.add(task)
        def finished(task):
            self.controls.discard(task)
            if task.cancelled():
                # A queued operation may be cancelled before execute begins.
                coroutine.close()
        task.add_done_callback(finished)
        return task

    async def dispatch(self, header, data):
        op = header.get("op")
        if op == "describe":
            description = describe(load(header["source"]))
            if len(json.dumps(description).encode("utf-8")) > 1024 * 1024:
                raise ValueError("description exceeds 1 MiB")
            send({"op": "described", "request": header["request"], "description": description})
        elif op == "create":
            node_id = header["node"]
            if node_id in self.nodes or node_id in self.starting or node_id in self.stopping:
                raise ValueError("node already exists or has a pending lifecycle operation")
            if len(self.nodes) + len(self.starting) >= 256:
                raise ValueError("node instance quota exceeded")
            task = self.control(self.create(header), header)
            self.starting[node_id] = task
            task.add_done_callback(lambda _, node_id=node_id: self.starting.pop(node_id, None))
        elif op == "packet":
            key = (header["node"], header["request"])
            if key in self.tasks:
                raise ValueError("duplicate packet request")
            if len(self.tasks) >= 256:
                raise ValueError("pending invocation quota exceeded")
            task = asyncio.create_task(self.packet(header, data))
            self.tasks[key] = task
            task.add_done_callback(lambda _, key=key: self.tasks.pop(key, None))
        elif op in ("call_output", "call_done"):
            state = self.calls.get((header["node"], header["request"]))
            if state is None or state["future"].done():
                return
            if op == "call_output":
                state["bytes"] += len(data)
                if state["bytes"] > state["limits"]["maxBytes"] or len(state["packets"]) >= state["limits"]["maxChunks"]:
                    state["future"].set_exception(ValueError("child output quota exceeded"))
                else:
                    port = header.get("port", 0)
                    if type(port) is not int or port < 0 or port > 1:
                        state["future"].set_exception(ValueError("invalid child output port"))
                    else:
                        state["packets"].append(ChildPacket(data, port))
            elif header.get("error"):
                state["future"].set_exception(RuntimeError(str(header["error"])[:32768]))
            else:
                state["future"].set_result(state["packets"])
        elif op == "cancel":
            self.control(self.cancel(header["node"], header["request"]), header)
        elif op == "shutdown":
            for node in set(self.nodes) | set(self.starting) | set(self.stopping):
                await self.cancel(node, 0)
            self.exit = True
        else:
            raise ValueError("unknown protocol operation")

    async def run(self):
        loop = asyncio.get_running_loop()
        def read():
            try:
                while True:
                    frame = read_frame()
                    asyncio.run_coroutine_threadsafe(self.queue.put(frame), loop).result()
            except BaseException:
                try:
                    asyncio.run_coroutine_threadsafe(self.queue.put((None, b"")), loop).result()
                except BaseException:
                    pass
        threading.Thread(target=read, daemon=True).start()
        while not self.exit:
            header, data = await self.queue.get()
            if header is None:
                break
            try:
                await self.dispatch(header, data)
            except BaseException:
                send({"op": "error", "request": header.get("request", 0), "node": header.get("node", ""), "error": error_text()})
        for node in set(self.nodes) | set(self.starting) | set(self.stopping):
            await self.cancel(node, 0)
        if self.controls:
            await asyncio.gather(*self.controls, return_exceptions=True)


if __name__ == "__main__":
    asyncio.run(Host().run())
