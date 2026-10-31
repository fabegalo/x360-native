"""Paired wall-clock runs of headless Native probes on any host.

Alternates the order of baseline and candidate every round, requires every
run to succeed, and reports medians, the paired ratio median and its range.
Variants run the candidate binary with cvars passed to the pipeline probe
(applied after the plugin loads), for example native_pipeline_threads=0 for
synchronous pipeline creation. Each arm gets its own Mesa shader cache
directory (warmed by --warmup runs) so a shared driver cache cannot favor one.
These are probe timings (pipeline creation, recording, submission and GPU work
of synthetic draws), not game frame rates.

  probe_pairs.py --baseline DIR --candidate DIR --output OUT.json
      [--rounds 7] [--probe pipeline] [--variant sync:native_pipeline_threads=0]
"""
import argparse
import json
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

PROBES = {
    'memexport': ('x360n_native_memexport_probe', ['vulkan']),
    'occlusion': ('x360n_native_occlusion_probe', ['vulkan']),
    # Reports its own doorbell-to-completion time (elapsed_ms).
    'pipeline': ('x360n_native_pipeline_probe', ['vulkan', '128']),
}


API_OVERRIDE = {}


def run(directory: Path, probe: str, cvars: list, cache: Path) -> float:
    executable, arguments = PROBES[probe]
    suffix = '.exe' if os.name == 'nt' else ''
    with tempfile.TemporaryDirectory(prefix='x360n-probe-') as output:
        start = time.perf_counter()
        if cvars and probe != 'pipeline':
            raise SystemExit('cvar variants are supported by the pipeline probe only')
        result = subprocess.run([str(directory / (executable + suffix)), output, *API_OVERRIDE.get(probe, arguments), *cvars],
                                env={**os.environ, 'MESA_SHADER_CACHE_DIR': str(cache)},
                                capture_output=True, text=True, timeout=600)
        elapsed = time.perf_counter() - start
    if result.returncode:
        raise SystemExit(f'{directory} {probe} failed ({result.returncode}): {result.stderr[-400:]}')
    for token in result.stdout.split():
        if token.startswith('elapsed_ms='):
            return float(token.split('=', 1)[1]) / 1000
    return elapsed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--rounds', type=int, default=7)
    parser.add_argument('--probe', nargs='+', default=list(PROBES), choices=list(PROBES))
    parser.add_argument('--variant', action='append', default=[],
                        help='name:cvar=value[,cvar=value]: candidate binary with these cvars')
    parser.add_argument('--warmup', type=int, default=2, help='unmeasured runs per arm and probe')
    parser.add_argument('--api', choices=['vulkan', 'd3d12'], default='vulkan')
    parser.add_argument('--draws', type=int, default=128, help='pipeline probe draws (1-250)')
    options = parser.parse_args()
    for probe, (_, arguments) in PROBES.items():
        API_OVERRIDE[probe] = [options.api] + ([str(options.draws)] if probe == 'pipeline' else [])
    arms = {'baseline': (options.baseline, []), 'candidate': (options.candidate, [])}
    for variant in options.variant:
        name, _, assignments = variant.partition(':')
        arms[name] = (options.candidate, [item for item in assignments.split(',') if item])
    report = {'api': options.api, 'draws': options.draws, 'host': platform.platform(), 'processor': platform.processor() or platform.machine(),
              'cpus': os.cpu_count(), 'rounds': options.rounds, 'warmup': options.warmup,
        'variants': {name: cvars for name, (_, cvars) in arms.items()}, 'probes': {}}
    caches = tempfile.TemporaryDirectory(prefix='x360n-driver-cache-')
    for probe in options.probe:
        times = {name: [] for name in arms}
        for name, (directory, cvars) in arms.items():
            for _ in range(options.warmup):
                run(directory, probe, cvars, Path(caches.name) / name)
        for round_index in range(options.rounds):
            order = list(arms)
            if round_index % 2:
                order.reverse()
            for name in order:
                directory, cvars = arms[name]
                times[name].append(run(directory, probe, cvars, Path(caches.name) / name))
        summary = {}
        for name, values in times.items():
            ratios = [value / base for value, base in zip(values, times['baseline'])]
            summary[name] = {'seconds': [round(value, 4) for value in values],
                             'median_s': round(statistics.median(values), 4),
                             'paired_ratio_median': round(statistics.median(ratios), 4),
                             'paired_ratio_min': round(min(ratios), 4),
                             'paired_ratio_max': round(max(ratios), 4)}
        report['probes'][probe] = summary
        for name, values in summary.items():
            print(f"{probe:10} {name:10} median {values['median_s']:.3f}s ratio {values['paired_ratio_median']:.3f} "
                  f"[{values['paired_ratio_min']:.3f}, {values['paired_ratio_max']:.3f}]")
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
