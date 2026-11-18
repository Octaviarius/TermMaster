"""Descriptors and bounded execution shared by inspection and the process host."""
import asyncio
import contextvars
import inspect
import math
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from types import MappingProxyType

_current = contextvars.ContextVar("termaster_packet", default=None)
_EXACT = 2 ** 53 - 1


def plain(value):
    if isinstance(value, Enum):
        return plain(value.value)
    if isinstance(value, Path):
        return str(value)
    return value


@dataclass(frozen=True)
class Condition:
    selector: object
    value: object


class Property:
    def __init__(self, type, *, default=None, minimum=None, maximum=None, step=None,
                 choices=None, title=None, group=None, help=None, widget=None,
                 when=None, order=None, read_only=False, **options):
        if type not in (bool, int, float, str, Path) and not (inspect.isclass(type) and issubclass(type, Enum)):
            raise TypeError("Property supports bool, int, float, str, Path and Enum")
        self.type = type
        self.default = default
        self.minimum = minimum
        self.maximum = maximum
        self.step = step
        self.choices = choices
        self.title = title
        self.group = group
        self.help = help
        self.widget = widget
        self.when = when
        self.order = order
        self.read_only = read_only
        self.options = options
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def __get__(self, instance, owner=None):
        return self if instance is None else instance._configuration[self.name]

    def __set__(self, instance, value):
        raise AttributeError("active configuration is immutable")

    def is_(self, value):
        return Condition(self, value)

    def values(self):
        if inspect.isclass(self.type) and issubclass(self.type, Enum):
            return [plain(v) for v in self.type]
        return list(self.choices) if self.choices is not None else None

    def validate(self, value, directory):
        kind = self.type
        raw = plain(value)
        if kind is bool and type(raw) is not bool:
            raise TypeError(f"{self.name} must be bool")
        if kind is int and (type(raw) is not int or abs(raw) > _EXACT):
            raise ValueError(f"{self.name} must be an exact-range integer")
        if kind is float:
            if type(raw) not in (float, int) or not math.isfinite(raw) or abs(raw) > _EXACT:
                raise ValueError(f"{self.name} must be a finite exact-range number")
            value = float(raw)
        if kind is str and not isinstance(raw, str):
            raise TypeError(f"{self.name} must be str")
        if kind is Path:
            if not isinstance(raw, str):
                raise TypeError(f"{self.name} must be a path string")
            value = Path(raw)
            if not value.is_absolute():
                value = Path(directory) / value
        if inspect.isclass(kind) and issubclass(kind, Enum):
            value = kind(raw)
        choices = self.values()
        if choices is not None and raw not in choices:
            raise ValueError(f"{self.name} is outside its choices")
        if self.minimum is not None and raw < self.minimum:
            raise ValueError(f"{self.name} is below minimum")
        if self.maximum is not None and raw > self.maximum:
            raise ValueError(f"{self.name} is above maximum")
        if self.step is not None and isinstance(raw, (int, float)):
            delta = (raw - (self.minimum or 0)) / self.step
            if not math.isclose(delta, round(delta), abs_tol=1e-9):
                raise ValueError(f"{self.name} does not match step")
        return value

    def schema(self, order):
        kind = self.type
        values = self.values()
        if inspect.isclass(kind) and issubclass(kind, Enum):
            if not values or len({type(v) for v in values}) != 1 or type(values[0]) not in (str, int, float, bool):
                raise ValueError("Enum values must have one supported scalar type")
            kind = type(values[0])
            if kind in (int, float) and any(not math.isfinite(v) or abs(v) > _EXACT for v in values):
                raise ValueError("Enum values exceed exact numeric range")
        result = {"type": {bool: "boolean", int: "integer", float: "number", str: "string", Path: "string"}[kind],
                  "title": self.title or self.name.replace("_", " ").capitalize(),
                  "default": plain(self.default), "x-settings-path": self.name,
                  "x-order": self.order if self.order is not None else order}
        for key, value in (("minimum", self.minimum), ("maximum", self.maximum),
                           ("multipleOf", self.step), ("description", self.help),
                           ("x-group", self.group), ("x-widget", self.widget)):
            if value is not None:
                result[key] = value
        if self.read_only:
            result["readOnly"] = True
        if values is not None:
            result["enum"] = values
        options = dict(self.options)
        if isinstance(self.choices, dict):
            options["labels"] = list(self.choices.values())
        if self.type is Path:
            result["x-widget"] = self.widget or ("directoryPath" if options.pop("kind", None) == "directory" else "filePath")
        if options:
            result["x-widget-options"] = options
        for value in (self.minimum, self.maximum, self.step):
            if value is not None and (type(value) not in (int, float) or not math.isfinite(value) or abs(value) > _EXACT):
                raise ValueError("numeric constraints exceed exact numeric range")
        if self.step is not None and self.step <= 0:
            raise ValueError("step must be positive")
        self.validate(self.default, ".")
        return result


class _Port:
    def __init__(self, type=bytes, *, title=None, flow=None):
        if type is not bytes:
            raise TypeError("ports carry bytes only")
        if flow not in (None, "rx", "tx"):
            raise ValueError("flow must be rx or tx")
        self.title = title
        self.flow = flow
        self.name = None

    def __set_name__(self, owner, name):
        self.name = name

    def __get__(self, instance, owner=None):
        return self if instance is None else BoundPort(instance, self)

    def __set__(self, instance, value):
        raise AttributeError("port binding is immutable")


class Input(_Port):
    pass


class Output(_Port):
    pass


class BoundPort:
    def __init__(self, instance, descriptor):
        self._instance = instance
        self._descriptor = descriptor

    def emit(self, data):
        if not isinstance(self._descriptor, Output):
            raise TypeError("only output ports emit")
        self._instance.ctx._emit(self._descriptor, data)


def handles(input, *, output=None):
    if not isinstance(input, Input) or (output is not None and not isinstance(output, Output)):
        raise TypeError("handles requires declared input/output descriptors")
    def decorate(function):
        function._termaster_handler = (input, output)
        return function
    return decorate


def processor(*, title=None, group=None, help=None):
    def decorate(cls):
        cls._metadata = {"title": title, "group": group, "help": help}
        return cls
    return decorate


class Node:
    def __init__(self, id, **configuration):
        if not isinstance(id, str) or not id:
            raise ValueError("child processor ID must be nonempty")
        self.id = id
        self.configuration = dict(configuration)

    def __set_name__(self, owner, name):
        self.name = name

    def __get__(self, instance, owner=None):
        return self if instance is None else BoundNode(instance, self)

    def __set__(self, instance, value):
        raise AttributeError("child binding is immutable")


class BoundNode:
    def __init__(self, instance, node):
        self._instance = instance
        self._node = node

    async def process(self, data, *, port=0, with_ports=False):
        results = await self._instance.ctx._call(self._node, packet(data), port)
        return results if with_ports else [result.data for result in results]


@dataclass(frozen=True)
class ChildPacket:
    """Owned child output; returned by Node.process(..., with_ports=True)."""
    data: bytes
    port: int


def descriptors(cls, kind):
    members = {}
    for base in reversed(cls.__mro__):
        for name, value in vars(base).items():
            if name in members:
                del members[name]
            if isinstance(value, kind):
                members[name] = value
    if kind in (Input, Output) and len(members) == 2:
        members = dict(sorted(members.items(), key=lambda item: 0 if item[1].flow == "rx" else 1))
    return members


def describe(cls):
    props = descriptors(cls, Property)
    inputs = descriptors(cls, Input)
    outputs = descriptors(cls, Output)
    if (len(inputs), len(outputs)) not in ((1, 1), (2, 2)):
        raise ValueError("Codeflow requires 1x1 or paired 2x2 ports")
    if len(inputs) == 2:
        for ports in (inputs, outputs):
            if {p.flow for p in ports.values()} != {"rx", "tx"}:
                raise ValueError("paired ports require one rx and one tx")
    handlers = handler_map(cls)
    if set(handlers) != set(inputs.values()):
        raise ValueError("each declared input requires exactly one handler")
    for _, output in handlers.values():
        if output is not None and output not in outputs.values():
            raise ValueError("handler output is not declared")
    fields = {name: prop.schema(i) for i, (name, prop) in enumerate(props.items())}
    schema = {"$schema": "http://json-schema.org/draft-07/schema#", "type": "object", "properties": {}, "required": []}
    conditions = [p.when for p in props.values() if p.when is not None]
    selector = None
    if conditions:
        selector = conditions[0].selector
        if selector not in props.values() or selector.when is not None or any(c.selector is not selector for c in conditions):
            raise ValueError("conditions require one unconditional finite selector")
        values = selector.values()
        if not values or len(values) < 2 or any(not isinstance(v, str) for v in values):
            raise ValueError("condition selector requires at least two string choices")
        if any(plain(c.value) not in values for c in conditions):
            raise ValueError("condition refers to unknown selector value")
        schema["oneOf"] = [{"title": str(v), "properties": {selector.name: {"const": v}}, "required": [selector.name]} for v in values]
    for name, prop in props.items():
        if prop.when is None:
            schema["properties"][name] = fields[name]
            schema["required"].append(name)
        else:
            branch = schema["oneOf"][selector.values().index(plain(prop.when.value))]
            branch["properties"][name] = fields[name]
            branch["required"].append(name)
    def ports(items):
        return [{"id": name, "title": p.title or name.replace("_", " ").capitalize(), "flow": p.flow or "rx", "type": "bytes"} for name, p in items.items()]
    title = getattr(cls, "_metadata", {}).get("title") or cls.__name__
    return {"className": cls.__name__, "title": title, "inputs": ports(inputs), "outputs": ports(outputs),
            "schema": schema, "defaults": {name: plain(p.default) for name, p in props.items()},
            "schemaDocument": {"version": 1, "type": "termaster.settings-schema", "description": title + " configuration", "content": schema},
            "nodes": {name: {"id": n.id, "configuration": n.configuration} for name, n in descriptors(cls, Node).items()}}


def handler_map(cls):
    methods = {}
    for base in reversed(cls.__mro__):
        methods.update(vars(base))
    result = {}
    for name, method in methods.items():
        declaration = getattr(method, "_termaster_handler", None)
        if declaration:
            input, output = declaration
            if input in result:
                raise ValueError("input has multiple handlers")
            result[input] = (name, output)
    return result


def packet(value):
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise TypeError("packet must be bytes, bytearray or memoryview")
    return bytes(value)


class Context:
    def __init__(self, emit, call, log, limits):
        self._publish = emit
        self._rpc = call
        self._logger = log
        self._max_bytes = limits.get("maxBytes", 16 * 1024 * 1024)
        self._max_chunks = limits.get("maxChunks", 1024)
        if type(self._max_bytes) is not int or type(self._max_chunks) is not int or self._max_bytes < 0 or self._max_chunks < 1:
            raise ValueError("invalid output limits")
        self._tasks = set()
        self._stopping = False

    @property
    def currentpacket(self):
        current = _current.get()
        return None if current is None else MappingProxyType({"request": current["request"], "port": current["port"], "data": current["data"]})

    @property
    def cancelled(self):
        return self._stopping

    def log(self, message, *, level="info"):
        self._logger(str(level), str(message)[:8192])

    def create_task(self, awaitable):
        if self._stopping:
            if inspect.iscoroutine(awaitable):
                awaitable.close()
            raise RuntimeError("processor is stopping")
        async def owned():
            token = _current.set(None)
            try:
                return await awaitable
            finally:
                _current.reset(token)
        task = asyncio.create_task(owned())
        self._tasks.add(task)
        def finished(task):
            self._tasks.discard(task)
            if not task.cancelled() and task.exception() is not None:
                self.log(str(task.exception()), level="error")
        task.add_done_callback(finished)
        return task

    def timer(self, delay, callback, *, repeat=False):
        if not isinstance(delay, (int, float)) or not math.isfinite(delay) or delay <= 0:
            raise ValueError("timer delay must be finite and positive")
        async def run():
            while True:
                await asyncio.sleep(delay)
                value = callback()
                if inspect.isawaitable(value):
                    await value
                if not repeat:
                    return
        return self.create_task(run())

    def _emit(self, output, data):
        current = _current.get()
        if self._stopping:
            raise RuntimeError("processor is stopping")
        if current is None:
            if asyncio.current_task() not in self._tasks:
                raise RuntimeError("output requires an active packet or owned task context")
            data = packet(data)
            if len(data) > self._max_bytes:
                raise ValueError("packet output quota exceeded")
            self._publish(output, data, 0)
            return
        if current.get("closed"):
            raise RuntimeError("packet invocation has completed")
        data = packet(data)
        if current["bytes"] + len(data) > self._max_bytes or len(current["outputs"]) >= self._max_chunks:
            raise ValueError("packet output quota exceeded")
        current["bytes"] += len(data)
        current["outputs"].append((output, data))

    async def _call(self, node, data, port):
        if self._stopping:
            raise RuntimeError("processor is stopping")
        if type(port) is not int or port < 0 or port > 1:
            raise ValueError("child port must be 0 or 1")
        if len(data) > self._max_bytes:
            raise ValueError("child input exceeds byte limit")
        return await self._rpc(node, data, port)

    async def close(self):
        self._stopping = True
        tasks = list(self._tasks)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


class Processor:
    def __init__(self, *, parameters=None, directory=".", context=None):
        describe(type(self))
        parameters = parameters or {}
        props = descriptors(type(self), Property)
        unknown = set(parameters) - set(props)
        if unknown:
            raise ValueError(f"unknown parameters: {sorted(unknown)}")
        self._configuration = MappingProxyType({name: p.validate(parameters.get(name, p.default), directory) for name, p in props.items()})
        self.ctx = context

    def start(self):
        pass

    def stop(self):
        pass

    async def _invoke(self, port, data, request):
        inputs = list(descriptors(type(self), Input).values())
        if type(port) is not int or port < 0 or port >= len(inputs):
            raise ValueError("invalid input port")
        method, output = handler_map(type(self))[inputs[port]]
        current = {"request": request, "port": port, "data": packet(data), "outputs": [], "bytes": 0, "closed": False}
        token = _current.set(current)
        try:
            result = getattr(self, method)(current["data"])
            if inspect.isawaitable(result):
                result = await result
            if result is not None:
                if current["outputs"]:
                    raise ValueError("handler cannot mix explicit emit and returned packets")
                if output is None:
                    raise ValueError("returned packets require a declared handler output")
                values = result if isinstance(result, (list, tuple)) else [result]
                for value in values:
                    self.ctx._emit(output, value)
            for target, value in current["outputs"]:
                self.ctx._publish(target, value, request)
        finally:
            current["closed"] = True
            _current.reset(token)
