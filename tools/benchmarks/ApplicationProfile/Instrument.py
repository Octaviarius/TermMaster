"""Temporarily insert aggregate scope timers, then restore byte-identical sources."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TARGETS = {
    'src/App/src/App.cpp': [
        'App::_updateRecent', 'App::_updateSession', 'App::_activateSession',
        'App::_activateWorkspace', 'App::saveSession'],
    'src/App/src/Gui/Terminal/TerminalController.cpp': [
        'TerminalController::_effective', 'TerminalController::_validate',
        'TerminalController::_materialize', 'TerminalController::applySettings',
        'TerminalController::closeAll', 'TerminalController::_close'],
    'src/App/src/Gui/Terminal/TerminalProcessingPanel.cpp': [
        'TerminalProcessingPanel::TerminalProcessingPanel', 'TerminalProcessingPanel::_catalog',
        'TerminalProcessingPanel::_showProperties', 'TerminalProcessingPanel::setConfiguration'],
    'src/Core/src/Workspace.cpp': [
        'Workspace::sync', 'Workspace::load', 'Workspace::_validateProcessors'],
    'src/Core/src/Session.cpp': ['Session::sync', 'Session::load', 'Session::_validate'],
    'src/Core/src/Settings.cpp': [
        'Settings::load', 'Settings::sync', 'Settings::value', 'Settings::applyValues'],
    'src/Core/src/SettingsSchema.cpp': [
        'SettingsSchema::SettingsSchema', 'SettingsSchema::validate', 'SettingsSchema::withDefaults'],
    'src/Core/src/Gui/Terminal/TerminalDisplay.cpp': [
        'TerminalDisplay::setAppearance', 'TerminalDisplay::_updateGeometry',
        'TerminalDisplay::paintEvent', 'TerminalDisplay::_layout'],
    'src/Core/src/Terminal/TerminalScreen.cpp': [
        'TerminalScreen::setLimits', 'TerminalScreen::_recolor'],
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def apply(backup):
    if backup.exists() and any(backup.iterdir()):
        raise RuntimeError('Use a new or empty backup directory')
    backup.mkdir(parents=True, exist_ok=True)
    outputs = {}
    for index, (relative, names) in enumerate(TARGETS.items()):
        original = (ROOT / relative).read_bytes()
        text = original.decode('utf-8')
        for name in names:
            position = text.find(name + '(')
            if position < 0:
                raise RuntimeError('Missing function ' + name)
            brace = text.find('\n{', position)
            if brace < 0:
                raise RuntimeError('Missing body ' + name)
            text = (text[:brace + 2] + f'\n    ApplicationProfileTrace::Scope profileScope("{name}");'
                    + text[brace + 2:])
        header = (ROOT / 'tools/benchmarks/ApplicationProfile/ProfileTrace.h').as_posix()
        edited = ('#include "' + header + '"\n' + text).encode('utf-8')
        outputs[relative] = (original, edited, index)

    entries = []
    for relative, (original, edited, index) in outputs.items():
        name = f'{index}.bin'
        (backup / name).write_bytes(original)
        entries.append({'path': relative, 'backup': name,
                        'instrumentedSha256': digest(edited), 'originalSha256': digest(original)})
    # Persist recovery metadata before the first production mutation.
    (backup / 'manifest.json').write_text(json.dumps(entries, indent=2), encoding='utf-8')
    for relative, (_, edited, _) in outputs.items():
        (ROOT / relative).write_bytes(edited)


def restore(backup):
    manifest = backup / 'manifest.json'
    entries = json.loads(manifest.read_text(encoding='utf-8'))
    for entry in entries:
        if entry['path'] not in TARGETS:
            raise RuntimeError('Unexpected source path in backup')
        if digest((backup / entry['backup']).read_bytes()) != entry['originalSha256']:
            raise RuntimeError('Backup was modified: ' + entry['path'])
        current = digest((ROOT / entry['path']).read_bytes())
        if current not in (entry['instrumentedSha256'], entry['originalSha256']):
            raise RuntimeError('Concurrent change detected: ' + entry['path'])
    for entry in entries:
        (ROOT / entry['path']).write_bytes((backup / entry['backup']).read_bytes())
    manifest.rename(backup / 'restored.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['apply', 'restore'])
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    if args.mode == 'apply':
        apply(args.backup)
    else:
        restore(args.backup)


if __name__ == '__main__':
    main()
