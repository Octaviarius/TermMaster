"""Temporarily insert aggregate scope timers, then restore byte-identical sources."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TARGETS = {
    'src/App/src/App.cpp': [
        'App::_ensureApplicationSettings', 'App::_applyConfiguration', 'App::restoreLastWorkspace', 'App::_updateRecent', 'App::_updateSession', 'App::_activateSession',
        'App::_activateWorkspace', 'App::saveSession'],
    'src/App/src/Gui/Terminal/TerminalController.cpp': [
        'TerminalController::_cleanupGroups', 'TerminalController::moveTerminal', 'TerminalController::moveGroup',
        'TerminalController::returnWindow', 'TerminalController::_rememberTabOrder',
        'TerminalController::_refreshGroupControls', 'TerminalController::_changed', 'TerminalController::_focusChanged', 'TerminalController::_surfaceCommands',
        'TerminalController::refreshTerminalActions', 'TerminalController::_effective', 'TerminalController::_validate',
        'TerminalController::_materialize', 'TerminalController::applySettings',
        'TerminalController::closeAll', 'TerminalController::_close'],
    'src/App/src/Gui/Terminal/TerminalWorkspace.cpp': [
        'TerminalWorkspace::appendWorkspace', 'TerminalWorkspace::transferGroup',
        'TerminalWorkspace::splitGroup', 'TerminalWorkspace::_setRoot', 'TerminalWorkspace::_prune', 'TerminalWorkspace::_createGroup'],
    'src/App/src/Gui/Terminal/TerminalTabGroup.cpp': [
        'TerminalTabGroup::_refreshFrame', 'TerminalTabGroup::setTabPresentation', 'TerminalTabGroup::TerminalTabGroup'],
    'src/App/src/Gui/Terminal/TerminalProcessingPanel.cpp': [
        'TerminalProcessingPanel::TerminalProcessingPanel', 'TerminalProcessingPanel::_catalog',
        'TerminalProcessingPanel::_showProperties', 'TerminalProcessingPanel::setConfiguration'],
    'src/Core/src/Plugin/PluginManager.cpp': [
        'PluginManager::start', 'PluginManager::_scan', 'PluginManager::_reconcile', 'PluginManager::setEnabled'],
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
        if relative.endswith(('TerminalController.cpp', 'TerminalWorkspace.cpp')):
            pattern = r'^( +)([\w.>\-()]+(?:->|\.)(setParent|addWidget|insertWidget|show|activate|removeTab|setCurrentIndex|setCurrentWidget)\([^;\n]*\));\r?$'
            text = re.sub(pattern, lambda match: match[1] + 'ApplicationProfileTrace::call("Qt.' + match[3]
                          + '", [&] { ' + match[2] + '; });', text, flags=re.MULTILINE)
            text = re.sub(r'(const int index = )(target->tabs\(\).insertTab\([\s\S]*?QString\(\)\))',
                          r'\1ApplicationProfileTrace::call("Qt.insertTab", [&] { return \2; })', text)
        if relative.endswith('TerminalTabGroup.cpp'):
            marker = 'ApplicationProfileTrace::Scope profileScope("TerminalTabGroup::_refreshFrame");'
            text = text.replace(marker, marker + '\n    if (qEnvironmentVariableIsSet("TERMASTER_PROFILE_NATIVE_GROUP_FRAME")) { if (!styleSheet().isEmpty()) setStyleSheet({}); setFrameShape(QFrame::Box); setLineWidth(2); return; }', 1)

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
