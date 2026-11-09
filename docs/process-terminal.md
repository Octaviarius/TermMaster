# Process terminal and existing-process attachment

Status: planned. Implementation and attachment experiments require a separate
start instruction. The delivered local-console backend remains unchanged.

## Intended scope

A separate ProcessTerminal plugin will connect the existing terminal display to
process stdin, stdout and stderr. It will offer two source modes: launch a new
process or select an existing process. Configuration will use the existing YAML
settings owner and generated forms with x-widget editors.

Launching needs an executable, arguments, working directory and environment.
Attaching needs an individual process identity and a supported attachment method.
Keep byte transport, future processing chains, terminal emulation and GUI separate.
Preserve stdout and stderr as distinct streams until an explicit merge policy;
stderr output alone is not a connection failure. Bound queued input/output, handle
partial writes and EOF, and release owned resources on plugin shutdown. A normal
process exit must not trigger an automatic restart by default.

## Process picker

Use Flowgate's process picker as presentation reference:
`F:/DEVELOPING/Octaviarius/Flowgate/src/gui/qml/ProcessPicker.qml` and
`src/gui/src/ProcessModel.hpp/.cpp`. That picker selects executable paths for
traffic rules and aggregates PIDs; it does not attach process stdio.

The Widgets implementation should use an asynchronously refreshed model, search
by name/path/PID, process icons and stable selection. Show the name prominently
and supporting path/PID/user information in muted text. Select an individual
process instance and revalidate PID plus creation identity before attaching to
avoid PID reuse. Explain unavailable attachment methods in the UI.

## Attachment options

There is no universal supported Windows API that acquires arbitrary running
process stdin/stdout/stderr as private byte streams.

| Method | Practical scope and limitation |
| --- | --- |
| Retained Termaster-owned pipes | Reattach a view to a process started with retained endpoints; process ownership must be independent of the tab. |
| Cooperative IPC | A target exposes an explicit endpoint and protocol. |
| Existing Windows console | AttachConsole and console screen/input APIs can mirror shared console state; this is not stdout byte interception. |
| Native DLL agent and API hooks | Experimental interception of future I/O inside a compatible target; requires architecture and API coverage research. |
| Replace standard handles in the target | Cached/duplicated handles and CRT buffering can bypass replacements. |
| Debugger interception | Debug events can suspend execution; unsuitable as the default interactive transport. |
| Linux reptyr-style takeover | ptrace/controlling-TTY operations are platform-specific and constrained; investigate separately. |

Output already discarded before attachment cannot generally be recovered.
Console screen snapshots preserve visible cells/attributes, not the original
stream or its full history. Detach and terminate must be separate commands;
detaching an externally started process must not terminate it. No persistent
system service is planned.

## Experimental Windows hook backend

Investigate Microsoft Detours as source material and a possible dependency;
it is not selected or added yet. Prefer a minimal native agent without Qt,
built for the target architecture, with authenticated local IPC to Termaster.

Expose explicit modes:

- Observe: mirror future output while preserving its original destination.
- Redirect/control: route supported output and provide input through the agent.

Initial API candidates are WriteFile, WriteConsoleA/W, ReadFile and ReadConsoleA/W.
Filter actual standard I/O handles rather than intercepting unrelated file I/O.
Account for duplicated/replaced/cached handles, CRT buffering, direct console
screen operations and unsupported API paths. Do not promise arbitrary-process
compatibility or full terminal behavior from these hooks alone.

Research must cover asynchronous completion and buffer lifetime, reads already
blocked at attachment, hook reentrancy, agent IPC bypass, ordering, bounded queues
and an explicit overflow policy. Avoid duplicate terminal replies when the
original console continues handling output. Detach must stop new hook work,
drain callbacks and safely remove hooks before unloading. Blocking teardown
must not run inside DllMain.

## Research sequence

Start with normal child-process pipes and lifecycle tests. Explore retained
owned endpoints and the process picker next. For invasive attachment, first
measure output observation on controlled synthetic targets, then investigate
input, asynchronous I/O and safe detach. Use dedicated fixtures for console
APIs, CRT streams, redirected handles and target architectures. Default tests
must not inject into unrelated processes, request elevation or alter services.

## References

- [Delivered local console](local-console.md)
- [Delivered socket terminal](socket-terminal.md)
- [Microsoft Detours documentation](https://github.com/microsoft/Detours/wiki)
- [AttachConsole](https://learn.microsoft.com/en-us/windows/console/attachconsole)
- [ReadConsoleOutput](https://learn.microsoft.com/en-us/windows/console/readconsoleoutput)
- [WriteConsoleInput](https://learn.microsoft.com/en-us/windows/console/writeconsoleinput)
- [DebugActiveProcess](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess)
- [QProcess](https://doc.qt.io/qt-6/qprocess.html)
- [reptyr upstream](https://github.com/nelhage/reptyr)
