"""Sequential paired headless runs; hard timeout and byte-for-byte comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import statistics
import subprocess
import ctypes
import time

class MemoryCounters(ctypes.Structure):
    _fields_ = [('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)] + [
        (name,ctypes.c_size_t) for name in ('peak_ws','ws','peak_paged','paged',
                                         'peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]

get_memory = ctypes.WinDLL('psapi').GetProcessMemoryInfo
get_memory.argtypes = [ctypes.c_void_p,ctypes.POINTER(MemoryCounters),ctypes.c_ulong]
get_memory.restype = ctypes.c_int

p = argparse.ArgumentParser()
p.add_argument('--baseline', required=True, type=Path)
p.add_argument('--candidate', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--rounds', type=int, default=7)
p.add_argument('--iterations', type=int, default=4096)
p.add_argument('--modes', nargs='+', default=['export', 'resolve', 'sync', 'none'])
p.add_argument('--scales', nargs='+', type=int, default=[1])
p.add_argument('--instrument', type=int, default=1, choices=[0, 1])
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
rows = []
deadline = time.monotonic() + 600
for mode in a.modes:
    for scale in a.scales:
        for pair in range(a.rounds):
            outputs = []
            order = [('baseline', a.baseline), ('candidate', a.candidate)]
            if pair % 2:
                order.reverse()
            for label, exe in order:
                exe = exe.resolve()
                out = (a.output / f'{mode}-{scale}-{pair}-{label}').resolve()
                out.mkdir(parents=True, exist_ok=True)
                with (out/'stdout.log').open('w') as log:
                    process = subprocess.Popen([str(exe), str(out), mode, str(a.iterations), str(scale), str(a.instrument)],
                                               cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT)
                    limit = min(deadline,time.monotonic()+90)
                    peak_ws = peak_private = 0
                    try:
                        while process.poll() is None:
                            if time.monotonic() >= limit:
                                raise TimeoutError('Headless probe deadline exceeded')
                            memory = MemoryCounters(); memory.cb=ctypes.sizeof(memory)
                            if get_memory(int(process._handle),ctypes.byref(memory),memory.cb):
                                peak_ws = max(peak_ws,memory.peak_ws)
                                peak_private = max(peak_private,memory.private)
                            time.sleep(.01)
                        if process.returncode:
                            raise subprocess.CalledProcessError(process.returncode,process.args)
                    finally:
                        if process.poll() is None:
                            process.kill(); process.wait(timeout=5)
                row = json.loads((out/'metrics.json').read_text())
                if not a.instrument:
                    # Disabled counters provide no evidence about GPU time,
                    # fences or allocations; do not report their zeros as data.
                    for key in ('submissions','waits','blocked_waits','wait_ms','submit_ms',
                                'gpu_ms','gpu_samples','readbacks','readback_bytes','buffers','peak_buffer_bytes'):
                        row[key] = None
                row.update(pair=pair, label=label, instrument=a.instrument,
                           peak_working_set_bytes=peak_ws or None, sampled_peak_private_bytes=peak_private or None,
                           exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                           bytes_sha256=hashlib.sha256((out/'bytes.bin').read_bytes()).hexdigest())
                rows.append(row)
                outputs.append((out/'bytes.bin').read_bytes())
                print(mode, scale, pair, label, row['wall_ms'], 'ms', flush=True)
            if outputs[0] != outputs[1]:
                raise RuntimeError(f'GPU/CPU bytes diverged: {mode}, scale {scale}, pair {pair}')
(a.output/'runs.json').write_text(json.dumps(rows, indent=2))
summary = []
for mode in a.modes:
    for scale in a.scales:
        old = [r for r in rows if r['mode']==mode and r['scale']==scale and r['label']=='baseline']
        new = [r for r in rows if r['mode']==mode and r['scale']==scale and r['label']=='candidate']
        ratios = [y['wall_ms']/x['wall_ms'] for x,y in zip(old,new)]
        rng = random.Random(360)
        boot = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(10000))
        result = dict(mode=mode, scale=scale, paired_wall_ratio_median=statistics.median(ratios),
                      bootstrap95=[boot[250],boot[9750]])
        for label, group in [('baseline',old),('candidate',new)]:
            result[label] = {k:statistics.median([r[k] for r in group]) for k in group[0]
                             if isinstance(group[0][k],(int,float)) and k not in ('pair','scale','instrument')}
            result[label].update({k:None for k in group[0] if group[0][k] is None})
        summary.append(result)
(a.output/'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
