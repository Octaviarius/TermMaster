# Command iconography

Application menus and terminal toolbars use a local Lucide SVG subset, pinned in
[source metadata](../components/lucide/SOURCE.md) with the upstream
[license](../components/lucide/LICENSE). No network access is needed at runtime or
during CMake configuration. Qt Svg renders the vectors.

## Presentation

Core's `Icons::icon()` creates a palette-aware QIcon engine. It resolves color at
paint time from the current QApplication palette: active ButtonText for normal
and hover, disabled ButtonText for disabled actions, and HighlightedText for
the Selected icon mode. Native menu styles choose their own icon mode. Existing icon instances follow palette changes without
rebuilding commands. Device-independent rendering preserves stroke weight at
fractional and integer device pixel ratios.

The terminal toolbar uses 24 logical pixel icons. Menus retain native Qt icon
metrics. Action icons use neutral text colors; connection status indicators keep
their separate state presentation. The application logo remains unchanged.

| Command | Lucide name |
| --- | --- |
| New console | square-terminal |
| New serial | cable |
| New socket | network |
| Connect / disconnect | plug / unplug |
| Start / stop console | play / square |
| Restart | rotate-cw |
| Refresh environment | refresh-cw |
| Clear screen | eraser |
| Settings | sliders-horizontal |
| Close | x |
| Reset setting override | undo-2 |

Workspace, session, editing and plugin-management commands use the same icon
family. Command IDs, shortcuts, order, eligibility and execution are unchanged.

The compact reset button keeps an orange background and an explicit dark icon
tint for contrast in both themes; disabled tinted icons use reduced opacity.

## Plugin boundary

PluginHost::Action may specify an `iconName` for a bundled host icon. Registration
resolves that name into a host-owned QIcon and rejects unknown names through the
existing invalid-action check. Plugins can still supply their own QIcon when no
bundled name is specified. Plugins depend on the SDK and Qt, never a second copy
of static Core or its icon engine. Plugins must be rebuilt against this SDK.

## Verification

IconTests covers an existing icon across palette changes, normal/active/disabled/
selected states, unknown names, transparent padding, 16/24 logical pixel sizes
and device pixel ratios 1, 1.5 and 2. The native-widget capture fixture contains a
QToolBar and QMenu with enabled, disabled and selected presentations.

Native light/dark references at 100%:
[light](images/icons/scale-1/IconsLight.png),
[dark](images/icons/scale-1/IconsDark.png).
At 200%: [light](images/icons/scale-2/IconsLight.png),
[dark](images/icons/scale-2/IconsDark.png).

Related application references: two-line tabs with type glyphs, selected state
and first-line close buttons in [light](images/icons/scale-1/TerminalTabsSelectedLight.png)
and [dark](images/icons/scale-1/TerminalTabsSelectedDark.png) palettes; the compact
orange reset beside a numeric spinbox at [100%](images/icons/scale-1/Settings.override.png)
and [200%](images/icons/scale-2/Settings.override.png).

Verified on 2026-11-09 with Qt 6.12.0 / MSVC 19.44 Debug. The normal
`build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug` application and plugin build passed;
all 26 configured CTest suites passed. An isolated `build/IconUxVerification`
build was used during development while the original plugin DLLs were in use.
Native Windows captures at scale factors 1 and 2 were visually inspected.
Settings checks include bounds, scientific notation, exact wide integers,
untouched invalid values across focus changes, direct/accessibility value
setters, enum precedence and reset geometry. Tab checks cover short and long
parameters, IPv6, inherited/local settings, restoration, selection/palette
changes, type icons, first-line close alignment and reordering. Fixtures use
temporary data and disconnected synthetic devices; no real serial-device or
external-network acceptance was performed.
