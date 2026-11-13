"""Aggregate docking and startup probe records without including fixture contents."""
import argparse
import json
import math
from pathlib import Path
import statistics
from collections import defaultdict


def aggregate(directory):
    values = defaultdict(list)
    failures = []
    for path in sorted(directory.glob('Run[0-9]*.json')):
        records = json.loads(path.read_text(encoding='utf-8'))['measurements']
        pending = {}
        for record in records:
            stage = record['stage']
            if not record['ok']:
                failures.append({'run': path.name, 'stage': stage, 'error': record.get('error', '')})
            if 'wallMs' not in record:
                continue
            values[stage].append(record['wallMs'])
            if 'fromProcessMainMs' in record:
                values['app.main.to.firstpaint'].append(record['fromProcessMainMs'])
            if stage.endswith('.sync'):
                pending[stage[:-5]] = record['wallMs']
            elif stage.endswith('.presentation') and stage[:-13] in pending:
                key = stage[:-13]
                values[key + '.total'].append(pending.pop(key) + record['wallMs'])
    runs = json.loads((directory / 'Runs.json').read_text(encoding='utf-8'))
    for run in runs['runs']:
        if run.get('launchToFirstPaintMs') is not None:
            values['app.launch.to.firstpaint'].append(run['launchToFirstPaintMs'])
        if run['exitCode'] or run['timedOut']:
            failures.append(run)
    result = {'sourceUnchanged': runs['sourceUnchanged'], 'failures': failures, 'milliseconds': {}}
    for stage, samples in sorted(values.items()):
        ordered = sorted(samples)
        result['milliseconds'][stage] = {
            'n': len(samples), 'median': statistics.median(samples),
            'p95': ordered[max(0, math.ceil(len(samples) * .95) - 1)], 'max': ordered[-1]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = aggregate(args.directory)
    (args.directory / 'Summary.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'failures': len(result['failures']), 'sourceUnchanged': result['sourceUnchanged'],
                      'stages': len(result['milliseconds'])}))
    if result['failures'] or not result['sourceUnchanged']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
