# Operation editor prototype

This standalone Qt 6 Widgets preview explores an operation tree on the left and
configuration for the selected operation on the right. Receive (Rx) and transmit
(Tx) chains use separate tabs. The receive example contains packet framing,
text decoding and terminal display. The transmit example contains a command,
a response wait and a repeat block with a command and a delay.

Selecting nodes and switching directions retains the existing in-memory draft
controls. The add action inserts a configurable placeholder into the selected
sequence or repeat block, or beside the selected leaf. This is an interaction
sketch: it has no transport, engine, scripts, persistence, validation contract,
drag/drop ordering or production application integration. Start is disabled.
Changing a value does not update the illustrative tree captions.

## Build and run

From the repository root in PowerShell with the installed Qt/MSVC toolchain:

```powershell
. ./.vscode/ExportEnvironment.ps1
cmake -S tools/previews/OperationEditor -B build/OperationEditorPrototype -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/OperationEditorPrototype
& ./build/OperationEditorPrototype/TermasterOperationEditorPreview.exe
```

The preview embeds existing local Lucide assets. No downloads or deployment
changes are required; launch it from the exported Qt environment.

## Captures and verification

```powershell
$env:QT_SCALE_FACTOR = '1'
& ./build/OperationEditorPrototype/TermasterOperationEditorPreview.exe --snapshot-dir docs/images/operation-editor
```

Snapshot mode selects the receive packet, transmit command and repeat block in
sequence, saves the live widget captures and exits with a failure status if any
capture cannot be saved. Each capture is 1100 by 760 logical pixels at scale 1.

- [Receive chain](images/operation-editor/Receive.png)
- [Transmit chain](images/operation-editor/Transmit.png)
- [Repeat block](images/operation-editor/Loop.png)

Verified with Qt 6.12.0 and MSVC x64 in Release: standalone configure/build,
successful snapshot-mode exit, visual inspection of all three captures and
`git diff --check`. No real device or network operations are performed.
