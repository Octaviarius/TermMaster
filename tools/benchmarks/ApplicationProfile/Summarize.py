"""Summarize successful instrumented-operation samples; keep raw timings and failures separate."""
import argparse
import json
from pathlib import Path
import statistics


def percentile(values, q):
    values = sorted(values)
    pos = (len(values) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = {}
    for directory in sorted(p for p in args.directory.iterdir() if p.is_dir()):
        stages = {}
        failures = []
        for path in sorted(directory.glob('Run[0-9]*.json')):
            for row in json.loads(path.read_text(encoding='utf-8'))['measurements']:
                if not row.get('ok', True):
                    failures.append(row)
                elif 'wallMs' in row:
                    stages.setdefault(row['stage'], []).append(row)
        summary = {}
        for name, rows in sorted(stages.items()):
            values = [row['wallMs'] for row in rows]
            summary[name] = {'n': len(values), 'medianMs': statistics.median(values),
                             'p95Ms': percentile(values, .95), 'minMs': min(values), 'maxMs': max(values)}
            cpu = [row['cpuMs'] for row in rows if 'cpuMs' in row]
            if cpu:
                summary[name]['medianCpuMs'] = statistics.median(cpu)
        result[directory.name] = {'stages': summary, 'failures': failures}
    (args.directory / 'Summary.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    for mode in ('release', 'debug', 'debug-instrumented'):
        if mode not in result:
            continue
        print(mode)
        for name, row in result[mode]['stages'].items():
            if (name.startswith(('session.', 'terminal.', 'workspace.', 'lineflow.', 'window.resize',
                                 'display.cold', 'display.warm.repaint', 'grid.warmresize.history10000'))):
                print(f"{name}: {row['medianMs']:.3f} ms median / {row['p95Ms']:.3f} p95 (n={row['n']})")


if __name__ == '__main__':
    main()
