"""Profile isolated copies of a local workspace without connecting restored terminals."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--qt', type=Path, default=Path('C:/Qt/6.12.0/msvc2022_64'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--dock', action='store_true', help='Synthetic disconnected docking and secondary-close profile')
    parser.add_argument('--restored-startup', action='store_true', help='Seed isolated last-workspace restoration before timed startup')
    parser.add_argument('--existing-preferences', action='store_true', help='Seed enabled bundled plugins for returning-user startup')
    parser.add_argument('--serial', action='store_true', help='Explicit opt-in: COM3 115200 loopback')
    parser.add_argument('--capture-stacks', action='store_true', help='Capture bounded local CDB stack on timeout')
    parser.add_argument('--timeout', type=int, default=75)
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.build = args.build.resolve()
    if args.source == args.scratch.resolve() or args.source in args.scratch.resolve().parents:
        raise ValueError('Scratch must be outside the original workspace tree')
    if args.source == args.output.resolve() or args.source in args.output.resolve().parents:
        raise ValueError('Output must be outside the original workspace tree')
    args.scratch.mkdir(parents=True, exist_ok=False)
    args.output.mkdir(parents=True, exist_ok=True)
    original = hashes(args.source)
    env = os.environ.copy()
    env['PATH'] = str(args.qt / 'bin') + os.pathsep + env.get('PATH', '')
    env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(args.qt / 'plugins/platforms')
    if args.serial and (args.dock or args.restored_startup):
        raise ValueError('Dock/startup controls cannot be combined with serial hardware opt-in')
    if args.dock:
        env['TERMASTER_PROFILE_DOCK_ONLY'] = '1'
    if args.restored_startup:
        env['TERMASTER_PROFILE_RESTORED_STARTUP'] = '1'
    statuses = []
    for index in range(args.repetitions):
        run = args.scratch / f'Run{index + 1}'
        shutil.copytree(args.source, run)
        (run / '.profile-copy').write_text('Isolated profiling copy\n', encoding='utf-8')
        prepare = [str(args.build / 'ApplicationProfile.exe'), '--prepare-fixture',
                   str(run / 'ws.termaster'), str(args.scratch / f'Data{index + 1}')]
        if args.restored_startup:
            prepare.append('--restored-startup')
        if args.existing_preferences:
            prepare.append('--existing-preferences')
        with (args.scratch / f'Prepare{index + 1}.log').open('w', encoding='utf-8') as log:
            subprocess.run(prepare, env=env, stdout=log, stderr=log, timeout=30, check=True)
        report = args.scratch / f'Run{index + 1}.json'
        command = [str(args.build / 'ApplicationProfile.exe'), str(run / 'ws.termaster'),
                   str(args.scratch / f'Data{index + 1}'), str(args.build / 'plugin'), str(report)]
        if args.serial and index == 0:
            command.append('--serial')
        log_path = args.scratch / f'Run{index + 1}.log'
        start = time.perf_counter()
        timed_out = False
        first_paint_ms = None
        with log_path.open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            try:
                if args.restored_startup:
                    marker = Path(str(report) + '.ready')
                    while process.poll() is None and not marker.exists() and time.perf_counter() - start < args.timeout:
                        time.sleep(0.005)
                    if marker.exists():
                        first_paint_ms = (time.perf_counter() - start) * 1000
                code = process.wait(timeout=max(0.001, args.timeout - (time.perf_counter() - start)))
            except subprocess.TimeoutExpired:
                timed_out = True
                if args.capture_stacks:
                    debugger = Path('C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe')
                    with (args.scratch / f'Run{index + 1}.stacks.txt').open('w', encoding='utf-8') as stacks:
                        try:
                            subprocess.run([str(debugger), '-sins', '-y', str(args.build), '-pv', '-p', str(process.pid),
                                            '-c', '~0 k 0n24; qd'], stdout=stacks, stderr=stacks, timeout=8)
                        except subprocess.TimeoutExpired:
                            stacks.write('\nCDB timeout; bounded capture stopped.\n')
                process.kill()
                code = process.wait()
        status = {'run': index + 1, 'exitCode': code, 'timedOut': timed_out,
                  'processWallSeconds': time.perf_counter() - start, 'launchToFirstPaintMs': first_paint_ms}
        statuses.append(status)
        if report.exists():
            data = json.loads(report.read_text(encoding='utf-8'))
            # Paths and fixture contents are not part of the published evidence.
            for item in data['measurements']:
                error = item.get('error', '')
                item['error'] = error.replace(str(args.source), '<source>').replace(str(args.scratch), '<scratch>')
            (args.output / f'Run{index + 1}.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
        scopes = []
        for line in log_path.read_text(encoding='utf-8', errors='replace').splitlines():
            if line.startswith('SCOPEJSON '):
                scopes.append(json.loads(line.removeprefix('SCOPEJSON ')))
        scopes = list({item['stage']: item for item in scopes}.values())
        if scopes:
            (args.output / f'Scopes{index + 1}.json').write_text(json.dumps(scopes, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(status), flush=True)
    unchanged = original == hashes(args.source)
    (args.output / 'Runs.json').write_text(json.dumps({'runs': statuses, 'sourceUnchanged': unchanged,
        'sourceFileCount': len(original), 'serialOptIn': args.serial, 'existingPreferences': args.existing_preferences,
        'modes': {key: env.get(key) for key in ('TERMASTER_PROFILE_CONFIGURED_ONLY', 'TERMASTER_PROFILE_NATIVE_GROUP_FRAME', 'TERMASTER_PROFILE_SCOPES', 'TERMASTER_PROFILE_DOCK_ONLY', 'TERMASTER_PROFILE_RESTORED_STARTUP', 'TERMASTER_PROFILE_ONE_CYCLE', 'TERMASTER_PROFILE_RESIZE_QUICK', 'TERMASTER_PROFILE_SKIP_TERMINAL', 'TERMASTER_PROFILE_CLOSE_ONLY', 'TERMASTER_PROFILE_RESIZE_ONLY', 'TERMASTER_PROFILE_DISPLAY_COMPARE', 'TERMASTER_PROFILE_DISPLAY_CONTENT', 'TERMASTER_PROFILE_DISPLAY_WRAP')},
        'exeSha256': hashlib.sha256((args.build/'ApplicationProfile.exe').read_bytes()).hexdigest()}, indent=2)+'\n',encoding='utf-8')
    if not unchanged:
        raise RuntimeError('Source workspace changed during profiling; inspect it before making a preservation claim')


if __name__ == '__main__':
    main()
