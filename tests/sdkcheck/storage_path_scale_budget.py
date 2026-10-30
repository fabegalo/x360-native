"""Opt-in local performance gate for the focused Storage scale test.

Run the C++ test with no other builds/games active, then supply its log and an
explicit machine/scenario budget. No timing threshold is imposed on normal CI.
"""

import argparse
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--log", type=Path, required=True)
parser.add_argument("--max-median-ms", type=float, required=True)
args = parser.parse_args()
if not 0 < args.max_median_ms < float("inf"):
    parser.error("the explicit median budget must be positive and finite")
text = args.log.read_text(encoding="utf-8-sig")
samples = re.findall(
    r"^storage-path-scale files=258 queries=4 sample=(\d+) elapsed_ms=([0-9.]+)$",
    text, re.MULTILINE,
)
if [index for index, _ in samples] != ["0", "1", "2"] or not re.search(
    r"All tests passed \(56 assertions in 1 test case\)", text
):
    parser.error("expected three complete samples and the passing correctness checks")
median = statistics.median(float(value) for _, value in samples)
passed = median <= args.max_median_ms
print(json.dumps({"log": str(args.log), "median_ms": median,
                  "max_median_ms": args.max_median_ms, "passed": passed}))
raise SystemExit(0 if passed else 1)
