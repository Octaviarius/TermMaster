# Socket terminal

SocketTerminal is an independent SHARED plugin. It registers the `socket` terminal
backend, generated connection/default settings and six icon actions in the Terminal
menu and toolbar. The plugin SDK includes the disconnected `createSocketDevice()`
factory. It links Qt Network and the header-only PluginApi, not another static Core.
The common terminal screen, renderer, input encoder and settings owners remain
unchanged. Future processing chains remain outside the byte transport.

## Settings

Terminal в†’ New socket terminal opens the standard settings layout. Existing socket
terminals use Terminal в†’ Socket terminal settings. Application, workspace and
session defaults inherit through the normal overlay; editing creates an override
and Reset restores the lower layer. Connection changes apply on the next attempt.
Descriptors use the version/type/description/content YAML envelope and existing
`x-widget` editors. Search filters fields and groups without discarding drafts.

| Field | Meaning | Default |
| --- | --- | --- |
| Protocol | TCP stream or UDP datagrams | TCP |
| Remote host | DNS name or numeric IPv4/IPv6 address | Required for a connection |
| Remote port | Destination port, 1вЂ“65535 | 5000 |
| Connect timeout | Bounds asynchronous name resolution/connection setup | 10000 ms |
| Local address | Optional numeric interface address, including an IPv6 scope ID | Automatic |
| Local port | Source port; zero requests an ephemeral port | 0 |
| Keep alive | TCP SO_KEEPALIVE using operating system probe timing | Enabled |
| No delay | TCP_NODELAY | Enabled |

All fields bind under `transport/`. Keys are `protocol`, `host`, `port`,
`connectTimeoutMs`, `bindAddress`, `bindPort`, `keepAlive` and `noDelay`.
TCP options are ignored for UDP. The destination is a raw TCP/UDP client endpoint;
Telnet negotiation, TLS, SSH, listening servers and multicast management are not
implemented by this plugin.

## Byte and lifecycle contracts

Opening is asynchronous. Open reports successful local setup/connection, not
application-level readiness. TCP performs a connection handshake; UDP only
resolves/selects a peer and prepares a local socket, so Open cannot establish that
the remote application is available. UDP incoming traffic is associated with the
selected peer, not a general-purpose listener.

TCP preserves the byte stream, including NUL, across arbitrary read boundaries.
One UDP `write` call sends one datagram, including an empty datagram. Datagrams
larger than 65507 bytes are rejected rather than split or truncated. Each received
UDP datagram produces one byte callback; the common terminal consumes callbacks
as a continuous display stream. Keyboard events, paste and generated terminal
replies retain their existing output boundaries. UDP does not guarantee delivery,
ordering or retransmission. No newline conversion or local echo is added by the
socket backend.

TCP's Qt input buffer is bounded to 256 KiB. UDP is read one bounded datagram at
a time; its kernel receive queue is managed by the operating system. Each read
pass limits byte/packet work and schedules remaining work through the GUI event loop. Pending output is bounded to
1 MiB including Qt's pending write buffer, with at most 1024 queued write entries.
Accepted bytes and completed native writes are separate notifications. They do not
prove receipt by the remote application. Short/zero stream writes retain pending
bytes. UDP frames are never silently shortened.

All socket objects, timers and callbacks run on the GUI thread. Each open creates
a fresh socket. Generation checks reject stale queued work after close/reopen.
Stop cancels DNS/connection attempts, aborts queued output and releases socket
resources. Normal TCP peer closure delivers buffered tail data before Closed.
Unexpected errors report a concrete cause and enter Failed. Application connection
intent uses the existing one-second retry policy; Stop clears intent before close
and cancels retry. UDP may only detect failures after traffic or OS error delivery.

Plugin shutdown saves the session, closes dependent forms and terminals, destroys
socket ownership and only then unregisters actions/factories. Persistence failure
prevents unloading. Session objects retain their identity and settings; re-enabling
the module reconstructs them disconnected.

## Verification

`TERMASTER_BUILD_SOCKET_TESTS=ON` adds SocketDeviceTests, SocketPluginTests and
SocketSettingsTests. Tests use loopback fixtures exclusively, with no external
network or physical devices. They cover binary streams, UDP boundaries and empty
frames, oversized writes, binding, peer close/tail data, cancellation, reopen,
IPv4/IPv6 loopback, reentrant close/reopen callbacks, configuration limits, registration rollback and retained teardown on failure.
Application acceptance exercises socket output through the terminal parser,
keyboard transmission, TCP automatic reconnect, explicit Stop, settings persistence
and unloading/reloading the actual plugin DLL.

Qt 6.12 / MSVC 2022 Debug builds the application and plugin. All 23 CTest suites
passed. Native Windows settings rendering/search was checked at 100% and 200%
scaling. The [1280 Г— 800 settings capture](images/socket/SocketSettings.png) uses
synthetic endpoint values. No remote-server acceptance or non-Windows execution
has been performed.
