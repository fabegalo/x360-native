"""Compare generated resolve resources with identical bounded GPU workloads.

Build x360n_gamma_resolve_gpu_checks Release first. Use --reference and
--candidate binary directories emitted by generate_resolves.py --binary-dir.
Each probe warms 30 batches and times 100 batches of 256 ordered resolves.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe", type=Path, default=root / ".local/build/win-amd64/tests/sdkcheck/g3/Release/x360n_gamma_resolve_gpu_checks.exe")
    p.add_argument("--reference", type=Path)
    p.add_argument("--candidate", required=True, type=Path)
    p.add_argument("--trials", type=int, default=5)
    p.add_argument("--msaa", type=int, nargs="+", default=[1, 2, 4])
    p.add_argument("--non-gamma", action="store_true")
    p.add_argument("--json", type=Path)
    args = p.parse_args()
    if not 1 <= args.trials <= 10 or any(n not in (1, 2, 4) for n in args.msaa):
        p.error("trials: 1..10; MSAA: 1,2,4")
    records = []
    for trial in range(args.trials):
        for samples in args.msaa:
            order = [("reference", args.reference), ("candidate", args.candidate)]
            if trial % 2:
                order.reverse()
            for label, directory in order:
                command = [str(args.probe), "--bench", "--msaa", str(samples)]
                if directory:
                    command += ["--shader-dir", str(directory.resolve())]
                if args.non_gamma:
                    command += ["--non-gamma"]
                result = subprocess.run(command, capture_output=True, text=True, timeout=90)
                print(f"trial={trial+1} samples={samples} variant={label}", flush=True)
                print(result.stdout, result.stderr, flush=True)
                result.check_returncode()
                for line in result.stdout.splitlines():
                    match = re.fullmatch(r"(D3D12|Vulkan) (.+) p50=([\d.e+]+) p95=([\d.e+]+) p99=([\d.e+]+) ns", line)
                    if match:
                        records.append(dict(trial=trial+1, samples=samples, variant=label,
                                            api=match[1], metric=match[2],
                                            p50=float(match[3]), p95=float(match[4]), p99=float(match[5])))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(records, indent=2) + "\n")


if __name__ == "__main__":
    main()
