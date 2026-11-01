"""A/B of two Native builds on the same recorded frames (x360n_native_replay).

Runs the baseline and candidate replay executables alternately over each
capture, requires every run to be equivalent to the capture and repeatable
(exit 0 of the replay), and reports per-phase means of the warm passes and the
paired ratio of the warm frame time. No gameplay is involved: the same frames
are replayed every time. Captures hold title data: keep them under .local/.

  native_replay_ab.py --baseline DIR --candidate DIR --capture FILE [FILE...]
      --output OUT.json [--backend d3d12|vulkan] [--rounds 7] [--repeat 10]
      [--cvar name=value ...] [--baseline-cvar name=value] [--candidate-cvar name=value]

The same build can be compared with itself (A/A noise) or with a cvar that
turns a change off in one arm (e.g. --baseline-cvar native_eager_submit=0).

DIR holds x360n_native_replay (or .exe) and the plugin/runtime libraries it
was built with. Exit 0: all runs equivalent; 3: a run differed from the
capture; 1: an error.
"""
import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

PHASES = ('frame_ms', 'renderer_ms', 'draw_ms', 'resolve_ms', 'sync_ms', 'sample_ms', 'present_ms',
          'submissions', 'fence_waits', 'fence_wait_ms', 'gpu_busy_ms', 'gpu_idle_ms')


def replay(directory: Path, capture: Path, args, extra: list) -> dict:
    executable = directory / ('x360n_native_replay.exe' if os.name == 'nt' else 'x360n_native_replay')
    with tempfile.TemporaryDirectory(prefix='x360n-replay-') as output:
        command = [str(executable), str(capture), output, '--repeat', str(args.repeat), '--stats']
        if args.backend:
            command += ['--backend', args.backend]
        command += args.cvar + extra
        result = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
        report_path = Path(output) / 'replay.json'
        report = json.loads(report_path.read_text()) if report_path.exists() else None
    if result.returncode == 3:
        raise SystemExit(f'{directory}: replay of {capture} differs from the capture: '
                         f'{(report or {}).get("mismatches", [])[:4]}')
    if result.returncode or not report:
        raise RuntimeError(f'{directory}: replay of {capture} failed ({result.returncode}): {result.stderr[-600:]}')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--capture', required=True, nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--backend', choices=['d3d12', 'vulkan'])
    parser.add_argument('--rounds', type=int, default=7)
    parser.add_argument('--repeat', type=int, default=10, help='replay passes per run (pass 0 is cold)')
    parser.add_argument('--cvar', action='append', default=[], help='name=value for both replays')
    parser.add_argument('--baseline-cvar', action='append', default=[], help='name=value for the baseline only')
    parser.add_argument('--candidate-cvar', action='append', default=[], help='name=value for the candidate only')
    parser.add_argument('--timeout', type=int, default=1800)
    args = parser.parse_args()
    report = {'host': platform.platform(), 'cpus': os.cpu_count(), 'rounds': args.rounds, 'repeat': args.repeat,
              'backend': args.backend, 'cvars': args.cvar, 'baseline_cvars': args.baseline_cvar,
              'candidate_cvars': args.candidate_cvar, 'captures': {}}
    for capture in args.capture:
        runs = {'baseline': [], 'candidate': []}
        for round_index in range(args.rounds):
            order = ['baseline', 'candidate'] if round_index % 2 == 0 else ['candidate', 'baseline']
            for arm in order:
                runs[arm].append(replay(args.baseline if arm == 'baseline' else args.candidate, capture, args,
                                        args.baseline_cvar if arm == 'baseline' else args.candidate_cvar))
        summary = {}
        for arm, results in runs.items():
            phases = {}
            for phase in PHASES:
                values = [r['per_frame'][phase] for r in results if r['per_frame'].get(phase) is not None]
                if values:
                    phases[phase] = round(statistics.median(values), 4)
            summary[arm] = {'median': phases, 'bottleneck': [r['per_frame']['bottleneck'] for r in results],
                            'equivalent_runs': sum(r['equivalent'] and r['repeatable'] for r in results)}
        ratios = [c['per_frame']['frame_ms'] / b['per_frame']['frame_ms']
                  for b, c in zip(runs['baseline'], runs['candidate']) if b['per_frame']['frame_ms']]
        summary['paired_frame_ratio'] = {'median': round(statistics.median(ratios), 4),
                                         'min': round(min(ratios), 4), 'max': round(max(ratios), 4)}
        report['captures'][capture.name] = summary
        base, cand = summary['baseline']['median'], summary['candidate']['median']
        print(f'{capture.name}: warm frame {base.get("frame_ms")} -> {cand.get("frame_ms")} ms, '
              f'ratio {summary["paired_frame_ratio"]["median"]} '
              f'[{summary["paired_frame_ratio"]["min"]}, {summary["paired_frame_ratio"]["max"]}]')
        for phase in PHASES[1:]:
            if phase in base or phase in cand:
                print(f'  {phase:14} {base.get(phase)} -> {cand.get(phase)}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
