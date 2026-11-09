# Local console terminal

The ConsoleTerminal SHARED plugin registers the `console` terminal type, launch
settings, an environment editor and icon actions in the Terminal menu/toolbar.
It uses the common project-owned terminal display and input encoder. It does not
introduce another terminal emulator or a background service.

## Launch and lifecycle

Terminal РІвЂ вЂ™ New console creates a disconnected session object. Start launches
the configured executable; Stop terminates the owned process tree; Restart closes
and launches it again. Terminal settings contain the executable, a YAML argument
array, working directory and environment overrides. An empty executable selects
PowerShell 7, Windows PowerShell or cmd on Windows, and `$SHELL` or `/bin/sh` on
Unix. Installed bash, ash and other shells can be selected by executable/path.
The shell executable uses the `consoleShellSelector` x-widget: an editable combo
with common platform commands. Windows suggestions include `pwsh.exe`,
`powershell.exe`, `cmd.exe`, `wsl.exe`, `bash.exe`, `sh.exe` and `ash.exe`; Unix
suggestions include `bash`, `sh`, `zsh`, `fish`, `dash`, `ash` and `pwsh`.
Suggestions do not imply installation. Any executable or script path can be entered
manually, including commands that may become available later. Empty input retains
automatic shell selection. The same descriptor/editor is used for scoped defaults,
terminal settings and terminal creation. Loading a value does not create an override.
Arguments are separate strings, not a command line parsed by the application.
Windows `.bat`/`.cmd` files use cmd; `.ps1` files use PowerShell `-File` without
changing execution policy. Batch paths/arguments reject quotes, percent signs
and line breaks because cmd cannot represent arbitrary literal argv.

Windows uses ConPTY with a job object. Redirected parent standard handles are
explicitly suppressed so the child attaches to ConPTY even when the application
was launched from an IDE or a redirected console. Unix uses a PTY, controlling terminal and
owned process group. A Unix descendant that deliberately creates a detached
session can escape process-group ownership. Resize events carry character-cell
geometry to the backend. Native workers handle byte I/O; callbacks and widgets
remain on the GUI thread. Both input/output queues are bounded to 1 MiB and writes
report accepted bytes separately from completed writes. Close cancels work and
joins workers before plugin unloading. The final received bytes precede the
natural-exit callback. A normal process exit clears connection intent, reports the
exit code in the tab tooltip and does not automatically relaunch the process.
Serial automatic reconnection remains enabled independently.

## Environment

The `consoleEnvironment` x-widget is a table editor with inherited values,
explicit overrides and removed variables. Editing an inherited value creates an
override. Add, remove and reset operate on the draft; Apply persists through the
existing settings owner. Only explicit overrides are saved. A null removes a
variable; an empty string preserves an empty value. Windows names compare without
case; Unix names are case-sensitive. Values are validated before process creation.
Application, workspace, session and terminal layers supply ordinary settings
inheritance. The environment baseline itself is never persisted or logged.

Each launch reads the user environment again and applies effective overrides.
Windows uses CreateEnvironmentBlock with inheritance disabled, including current
user/machine environment definitions rather than the development toolchain's
stale process environment. Unix uses the application's process environment;
arbitrary shell startup files are not executed for environment discovery.

Two commands are available:

- **Refresh user environment for next launch** refreshes open editor baselines
  without modifying overrides, marking drafts dirty or stopping running shells.
- **Refresh user environment and restart console** refreshes baselines and
  explicitly stops/starts the selected local console with the latest environment.

A running process's environment cannot be universally replaced from outside.
The first command therefore applies refreshed values to future launches; it does
not inject shell-specific commands into a running shell. Shell profile scripts may
further change the child environment. Restart terminates processes owned by that
terminal. Environment editing does not write the user's OS environment settings.

## Administrator shells on Windows

Select an executable in the editable shell combo and enable **Run as administrator**
for an elevated instance of that shell. The `transport/runAsAdministrator` boolean
inherits like other launch settings and defaults to false. Start requests Windows
UAC approval for a separate `Helpers/ConsoleHost.exe`; changing the checkbox does
not itself request elevation. Termaster retains its current token and the terminal
stays embedded in its existing tab. Cancellation is reported as a failed launch
without an automatic UAC retry. This mode is Windows-only; Unix can still select
`sudo` explicitly with suitable arguments.

ConsoleHost is a one-shot QCoreApplication endpoint, not a service. It owns the
existing native ConPTY implementation and job/process tree. The main application
uses a bounded local named-pipe protocol for configuration, bytes, geometry,
completed writes and exit status. Both endpoints check the peer process ID; a
fresh random nonce authenticates the helper before configuration is transmitted.
The command line contains pipe/nonce/PID tokens, not shell arguments or environment
values. Default launch uses the Windows `runas` verb and retains the process handle
returned by ShellExecuteEx, including when UAC uses another administrator account.

The helper verifies token elevation before accepting production work. It exits on
channel closure or parent death, terminates its owned shell tree and drains native
workers. Data output pauses native reading under IPC backpressure; input has a
1 MiB outstanding-byte credit and completion acknowledges native writes rather than
pipe buffering. Frames, receive buffers and pending queues have explicit limits.
A separate `--test-mode` runs the same host at the caller's existing privilege level
for deterministic tests; the production launcher never passes that option.

Each elevated launch reads the helper account's environment and applies the saved
explicit overrides. If UAC selects another account, its environment and executable
search paths may differ from the initiating user's environment editor preview.
Environment refresh without restart still leaves running shells unchanged.

Building ConsoleTerminal also builds ConsoleHost and copies its Qt Core/Network
runtime into `Helpers/`. Deploy that folder beside the application. Its runtime
DLLs stay outside plugin discovery, and the helper does not depend on a development
Qt/MSVC PATH inherited through UAC. No elevated acceptance is part of default tests.
The Windows consent dialog and a real elevated token require separate manual
acceptance; automated IPC tests launch the helper without requesting elevation.

## SDK and verification

HostedPlugin ABI version 4 adds host messages, optional device geometry,
reconnection policy and natural process-exit callbacks. Rebuild hosted plugins
against the current SDK; no compatibility aliases are provided.

`TERMASTER_BUILD_CONSOLE_TESTS=ON` enables the deterministic native ConsoleChild
fixture, backend lifecycle/byte/resize/queue tests, environment validation/editor
tests and plugin registration/rollback tests. Fixtures are executable test inputs,
not production plugins. Application integration also launches PowerShell with
`-NoProfile -NonInteractive` (or `/bin/sh` on Unix), checks displayed output and
natural exit, and unloads the plugin. No serial hardware or external network is
used. Terminal protocol compatibility remains the subset documented in
[terminal emulation](terminal-emulation.md).

Windows verification with Qt 6.12 / MSVC 2022: the Debug application and both
production plugins build, and all 20 CTest suites pass. The native fixture covers
actual console handles, byte input/output, UTF-8, argv quoting, environment
application, resize, natural exit, output draining, partial queued writes,
blocked-write cancellation, child-tree shutdown and batch paths with spaces.
Application acceptance displays output from a real profile-free PowerShell child,
observes exit code 7, and unloads terminal/settings factories. The environment
editor was rendered with the Windows Qt platform at 100% and 200% scaling; its
[1280 × 800 capture](images/local-console/ConsoleEnvironment.png) contains only
synthetic environment values. The POSIX implementation has not been built or run
on this Windows host.

The editable shell selector was checked for preset selection, arbitrary manual
paths, empty/automatic values, unchanged-draft loading and invalid-value
preservation. A [1280 × 800 popup capture](images/local-console/ShellSelector.png)
shows the actual Windows widget with synthetic configuration.

Administrator-host verification adds bounded/fragmented protocol checks and an
actual helper/ConPTY round trip at the test process's existing privilege level.
All 25 Debug CTest suites passed, including UTF-8, resize, output backpressure,
write credit, invalid authentication, cancellation, reentrant owner replacement
and reopen from Closed. The helper also starts with PATH removed, using its
private runtime folder. No UAC prompt or real elevated-token acceptance was run.

The native 1280 × 800 settings capture includes the editable dropdown and the
administrator flag in the generated Launch group. Environment fields were filtered
out before capture; the preview edits only a draft and never starts a shell or UAC.

Future process-pipe terminals and existing-process attachment options are recorded
in [process terminal planning](process-terminal.md). Implementation remains planned
and requires a separate start instruction.

## Intermittent acceptance observation (2026-11-09)

During the FLOW-8 names/flip full Debug suite, ConsoleElevationTests terminated
with Windows heap-corruption status 0xc0000374 after reporting the received row of
ownerReleaseInCallback as passed. The earlier full suite passed. One immediate
isolated rerun and three subsequent isolated runs passed all ten cases without
skips. The cause is not confirmed. The target does not link Core and its console
implementation and test sources were unchanged in FLOW-8. Review identified a
possible existing socket/event-dispatch lifetime hazard when a received callback
releases the device owner, but no crash dump establishes that as the cause.
No console source change is included with the processing/editor feature.
