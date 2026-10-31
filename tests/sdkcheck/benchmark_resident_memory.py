"""Paired costs of the production Xenos SharedMemory CPU service, never FPS."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=7)
    args = parser.parse_args()
    if not 3 <= args.rounds <= 15:
        parser.error('rounds must be between 3 and 15')
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    deadline = time.monotonic() + 240
    for pair in range(args.rounds):
        order = [('baseline', args.baseline), ('candidate', args.candidate)]
        if pair % 2:
            order.reverse()
        for label, executable in order:
            executable = executable.resolve(strict=True)
            run = subprocess.run([str(executable), '--benchmark'], cwd=executable.parent,
                                 capture_output=True, timeout=min(30, max(1, deadline-time.monotonic())))
            (args.output / f'{pair}-{label}.log').write_bytes(run.stdout + run.stderr)
            run.check_returncode()
            metrics = [json.loads(line) for line in run.stdout.splitlines() if line.startswith(b'{')]
            if [m['mode'] for m in metrics] != [0, 1, 2, 3]:
                raise ValueError('Missing or reordered workloads')
            for metric in metrics:
                if metric['iterations'] <= 0 or metric['ns'] <= 0:
                    raise ValueError('Invalid measurements')
                expected = metric['iterations'] if metric['mode'] == 3 else 0
                if metric['uploads'] != expected:
                    raise ValueError('Residency contract changed in measured workload')
                rows.append(dict(metric, pair=pair, label=label,
                                 exe_sha256=hashlib.sha256(executable.read_bytes()).hexdigest()))
    summary = []
    for mode in range(4):
        old = [r for r in rows if r['mode'] == mode and r['label'] == 'baseline']
        new = [r for r in rows if r['mode'] == mode and r['label'] == 'candidate']
        summary.append(dict(mode=mode,
                            baseline_ns=statistics.median(r['ns']/r['iterations'] for r in old),
                            candidate_ns=statistics.median(r['ns']/r['iterations'] for r in new),
                            paired_ratio=statistics.median(n['ns']/o['ns'] for o, n in zip(old, new))))
    result = dict(kind='SharedMemory CPU operation costs; not frames/FPS',
                  host=platform.platform(), runs=rows, summary=summary)
    (args.output/'results.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
