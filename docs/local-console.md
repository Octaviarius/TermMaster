# Local console terminal

The ConsoleTerminal SHARED plugin registers the `console` terminal type, launch
settings, an environment editor and icon actions in the Terminal menu/toolbar.
It uses the common project-owned terminal display and input encoder. It does not
introduce another terminal emulator or a background service.

## Launch and lifecycle

Terminal РІвЂ вЂ™ New local console creates a disconnected session object. Start launches
the configured executable; Stop terminates the owned process tree; Restart closes
and launches it again. Terminal settings contain the executable, a YAML argument
array, working directory and environment overrides. An empty executable selects
PowerShell 7, Windows PowerShell or cmd on Windows, and `$SHELL` or `/bin/sh` on
Unix. Installed bash, ash and other shells can be selected by executable/path.
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
