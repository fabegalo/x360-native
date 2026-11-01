"""A/B of Native builds or settings on the same recorded frames (x360n_native_replay).

Every arm replays the same captures in rotating order. A run counts only if
the replay exit code is 0 and its report says equivalent, repeatable and
visually checked on every frame (presenter output compared): equivalence is
mandatory, a single failing run aborts with exit 3. No gameplay is involved.
Captures hold title data: keep them under .local/.

  native_replay_ab.py --capture FILE [FILE...] --output OUT.json
      (--arm NAME DIR [cvar=value ...])... | --baseline DIR --candidate DIR
      [--baseline-cvar name=value] [--candidate-cvar name=value]
      [--backend d3d12|vulkan] [--rounds 7] [--repeat 10] [--cvar name=value ...]
      [--fail-on-regression]

The first arm is the reference. DIR holds x360n_native_replay(.exe) with the
plugin/runtime it was built with; two DIRs compare two builds, one DIR with
different cvars compares settings (e.g. native_eager_submit=0 vs 64), and
the same DIR and cvars twice measures the noise (A/A).

Per arm and capture, from the warm passes (pass 0 is cold) of every run:
mean/P50/P95/P99 of frame time, renderer time, renderer CPU time not waiting
on fences, fence waits, GPU busy and idle time, submissions and draws.
Per non-reference arm: paired ratio of each round's mean frame time to the
reference's, with a bootstrap 95% interval of the median ratio; the verdict
is "faster" or "slower" only when the interval excludes 1, otherwise
"inconclusive". Exit 0 when every run was equivalent (4 with
--fail-on-regression if an arm is "slower"), 3 on a non-equivalent run, 1 on
an error.
"""
import argparse
import csv
import json
import math
import os
import platform
import random
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

METRICS = ('frame_ms', 'renderer_ms', 'renderer_cpu_ms', 'fence_wait_ms', 'gpu_busy_ms', 'gpu_idle_ms',
           'draw_ms', 'resolve_ms', 'sync_ms', 'sample_ms', 'present_ms', 'submissions', 'fence_waits', 'draws')


class NotEquivalent(Exception):
    pass


def number(text):
    return float(text) if text not in (None, '') else None


def warm_frames(path: Path) -> list:
    """Per-frame metrics of the warm passes in a replay frames.csv."""
    frames = []
    with path.open(newline='') as handle:
        for row in csv.DictReader(handle):
            if int(row['pass']) == 0:
                continue
            frame = {'frame_ms': number(row['frame_ms']), 'renderer_ms': number(row['backend_ms']),
                     'fence_wait_ms': number(row['fence_wait_ms']), 'gpu_busy_ms': number(row['gpu_busy_ms']),
                     'draw_ms': number(row['draw_ms']), 'resolve_ms': number(row['resolve_ms']),
                     'sync_ms': number(row['sync_ms']), 'sample_ms': number(row['sample_ms']),
                     'present_ms': number(row['present_ms']), 'submissions': number(row['submissions']),
                     'fence_waits': number(row['fence_waits']), 'draws': number(row['draws'])}
            if frame['fence_wait_ms'] is not None:
                frame['renderer_cpu_ms'] = max(0.0, frame['renderer_ms'] - frame['fence_wait_ms'])
            if frame['gpu_busy_ms'] is not None:
                frame['gpu_idle_ms'] = max(0.0, frame['frame_ms'] - frame['gpu_busy_ms'])
            frames.append(frame)
    return frames


def replay(directory: Path, capture: Path, args, cvars: list) -> list:
    executable = directory / ('x360n_native_replay.exe' if os.name == 'nt' else 'x360n_native_replay')
    with tempfile.TemporaryDirectory(prefix='x360n-replay-') as output:
        command = [str(executable), str(capture), output, '--repeat', str(args.repeat), '--stats']
        if args.backend:
            command += ['--backend', args.backend]
        command += args.cvar + cvars
        result = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
        report_path = Path(output) / 'replay.json'
        report = json.loads(report_path.read_text()) if report_path.exists() else None
        if result.returncode == 3:
            raise NotEquivalent(f'{directory} {cvars}: replay of {capture.name} differs from the capture: '
                                f'{(report or {}).get("mismatches", [])[:4]}')
        if result.returncode or not report:
            raise RuntimeError(f'{directory} {cvars}: replay of {capture.name} failed ({result.returncode}): '
                               f'{result.stderr[-800:]}')
        # Exit 0 already means equivalent and repeatable; the visual check is
        # mandatory here as well (a --no-present run is not a proof).
        if not (report['equivalent'] and report['repeatable'] and report.get('visual_checked')
                and report['output_checks'] == report['frames']):
            raise NotEquivalent(f'{directory} {cvars}: replay of {capture.name} was not fully checked: '
                                f'equivalent={report["equivalent"]} repeatable={report["repeatable"]} '
                                f'visual={report.get("visual_checked")} output_checks={report["output_checks"]}/'
                                f'{report["frames"]}')
        frames = warm_frames(Path(output) / 'frames.csv')
    if not frames:
        raise RuntimeError(f'{capture.name}: no warm frames (use --repeat 2 or more)')
    return frames


def percentile(values: list, q: float) -> float:
    ordered = sorted(values)
    rank = max(1, math.ceil(q * len(ordered)))
    return ordered[min(rank, len(ordered)) - 1]


def describe(frames: list) -> dict:
    summary = {}
    for metric in METRICS:
        values = [f[metric] for f in frames if f.get(metric) is not None]
        if values:
            summary[metric] = {'mean': round(statistics.fmean(values), 4), 'p50': round(percentile(values, 0.5), 4),
                               'p95': round(percentile(values, 0.95), 4), 'p99': round(percentile(values, 0.99), 4)}
    return summary


def bootstrap_median(ratios: list, seed: int = 20261101, samples: int = 4000) -> tuple:
    rng = random.Random(seed)
    medians = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(samples))
    return medians[int(0.025 * samples)], medians[int(0.975 * samples) - 1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--arm', nargs='+', action='append', default=[], metavar='NAME DIR [cvar=value]',
                        help='an arm: name, directory with the replay, optional cvars; the first is the reference')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--baseline-cvar', action='append', default=[])
    parser.add_argument('--candidate-cvar', action='append', default=[])
    parser.add_argument('--capture', required=True, nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--backend', choices=['d3d12', 'vulkan'])
    parser.add_argument('--rounds', type=int, default=7)
    parser.add_argument('--repeat', type=int, default=10, help='replay passes per run (pass 0 is cold)')
    parser.add_argument('--cvar', action='append', default=[], help='name=value for every arm')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--fail-on-regression', action='store_true')
    args = parser.parse_args()
    arms = []
    if args.baseline or args.candidate:
        if not (args.baseline and args.candidate):
            parser.error('--baseline and --candidate go together')
        arms += [('baseline', args.baseline, args.baseline_cvar), ('candidate', args.candidate, args.candidate_cvar)]
    for spec in args.arm:
        if len(spec) < 2 or any('=' not in value for value in spec[2:]):
            parser.error(f'--arm NAME DIR [cvar=value ...], got {spec}')
        arms.append((spec[0], Path(spec[1]), spec[2:]))
    if len(arms) < 2 or len({name for name, _, _ in arms}) != len(arms):
        parser.error('at least two arms with distinct names are required')
    if args.repeat < 2 or args.rounds < 2:
        parser.error('--repeat and --rounds must be at least 2')

    report = {'host': platform.platform(), 'cpus': os.cpu_count(), 'rounds': args.rounds, 'repeat': args.repeat,
              'backend': args.backend, 'cvars': args.cvar,
              'arms': [{'name': n, 'directory': str(d), 'cvars': c} for n, d, c in arms], 'captures': {}}
    regression = False
    for capture in args.capture:
        runs = {name: [] for name, _, _ in arms}
        for round_index in range(args.rounds):
            # Rotate the order each round so no arm always runs first or last.
            order = arms[round_index % len(arms):] + arms[:round_index % len(arms)]
            for name, directory, cvars in order:
                runs[name].append(replay(directory, capture, args, cvars))
        reference = arms[0][0]
        result = {}
        for name, _, _ in arms:
            frames = [frame for run in runs[name] for frame in run]
            result[name] = {'warm_frames': len(frames), 'metrics': describe(frames)}
            if name == reference:
                continue
            ratios = [statistics.fmean(f['frame_ms'] for f in candidate) / statistics.fmean(f['frame_ms'] for f in base)
                      for base, candidate in zip(runs[reference], runs[name])]
            low, high = bootstrap_median(ratios)
            verdict = 'faster' if high < 1 else 'slower' if low > 1 else 'inconclusive'
            regression = regression or verdict == 'slower'
            result[name]['paired_frame_ratio'] = {'median': round(statistics.median(ratios), 4),
                                                  'ci95': [round(low, 4), round(high, 4)],
                                                  'rounds': [round(r, 4) for r in ratios], 'verdict': verdict}
        report['captures'][capture.name] = result
        print(f'{capture.name} ({args.rounds} rounds x {args.repeat - 1} warm passes, all runs equivalent)')
        header = f'  {"arm":14} {"frame p50/p95/p99 ms":28} {"renderer cpu":>12} {"fence":>8} {"gpu busy":>9} ' \
                 f'{"gpu idle":>9} {"subm":>6}  ratio [95% CI]'
        print(header)
        for name, _, _ in arms:
            m = result[name]['metrics']
            def get(metric, key='mean'):
                value = m.get(metric, {}).get(key)
                return '-' if value is None else f'{value:.2f}'
            frame = f'{get("frame_ms", "p50")}/{get("frame_ms", "p95")}/{get("frame_ms", "p99")}'
            ratio = result[name].get('paired_frame_ratio')
            text = '(reference)' if not ratio else \
                f'{ratio["median"]} [{ratio["ci95"][0]}, {ratio["ci95"][1]}] {ratio["verdict"]}'
            print(f'  {name:14} {frame:28} {get("renderer_cpu_ms"):>12} {get("fence_wait_ms"):>8} '
                  f'{get("gpu_busy_ms"):>9} {get("gpu_idle_ms"):>9} {get("submissions"):>6}  {text}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 4 if regression and args.fail_on_regression else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except NotEquivalent as error:
        print(error, file=sys.stderr)
        sys.exit(3)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
