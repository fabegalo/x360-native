#!/usr/bin/env python3
"""Apply, check or revert the local ReXGlue SDK patches of deps.lock.json.

Linux/macOS entry point with the same contract as apply-sdk-patches.ps1:
patches only for the pinned commit, `git apply` on the working tree (nothing is
committed in the submodule), idempotent, and every patched file must end with
the SHA-256 recorded in the lock (also verified by CMake at configure time).

The already-applied prefix is found from the recorded hashes alone, so a fully
patched checkout is confirmed without running `git apply` per patch.

Usage: scripts/setup/apply-sdk-patches.py [--check | --revert]
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "external" / "rexglue-sdk"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1 << 20), b""):
                digest.update(block)
    except FileNotFoundError:
        return ""
    return digest.hexdigest()


def git(*args: str, quiet: bool = False) -> int:
    result = subprocess.run(["git", "-C", str(SDK), *args], capture_output=True, text=True)
    if not quiet:
        for line in (result.stdout + result.stderr).splitlines():
            print(f"  {line}")
    return result.returncode


def applied_prefix(patches: list) -> int:
    """Largest n such that the tracked files equal the state after patch n."""
    snapshot = {}
    for patch in patches:
        for name, digest in patch["files"].items():
            if snapshot.get(name, digest) != digest:
                raise SystemExit(f"{name}: inconsistent final hashes in deps.lock.json")
            snapshot[name] = digest
    actual = {name: sha256(SDK / name) for name in snapshot}
    for count in range(len(patches), -1, -1):
        if all(actual[name] == digest for name, digest in snapshot.items()):
            return count
        if count == 0:
            break
        base = patches[count - 1].get("base_files", {})
        for name in patches[count - 1]["files"]:
            snapshot[name] = base.get(name, "")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="report only; exit 1 if anything is missing")
    mode.add_argument("--revert", action="store_true", help="reverse-apply, newest first")
    options = parser.parse_args()

    entry = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))["rexglue_sdk"]
    patches = list(entry.get("patches", []))
    if not patches:
        print("no SDK patches in deps.lock.json")
        return 0
    head = subprocess.run(["git", "-C", str(SDK), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != entry["commit"]:
        print(f"external/rexglue-sdk is at {head}, the patches are for {entry['commit']}; not touching it")
        return 1

    def patch_path(patch: dict) -> Path:
        path = ROOT / patch["file"]
        actual = sha256(path)
        if actual != patch["sha256"]:
            raise SystemExit(f"{patch['file']}: SHA-256 {actual}, lock says {patch['sha256']}")
        return path

    def is_applied(path: Path) -> bool:
        return git("apply", "--reverse", "--check", str(path), quiet=True) == 0

    def final_hashes_match(patch: dict) -> bool:
        files = patch["files"]
        return bool(files) and all(sha256(SDK / name) == digest for name, digest in files.items())

    prefix = 0 if options.revert else applied_prefix(patches)
    bad = 0
    ordered = list(reversed(patches)) if options.revert else patches
    for index, patch in enumerate(ordered):
        path = patch_path(patch)
        if options.revert:
            if not is_applied(path):
                print(f"{patch['file']}: not applied, skipping")
                continue
            if git("apply", "--reverse", str(path)) != 0:
                raise SystemExit(f"{patch['file']}: reverse apply failed")
            print(f"{patch['file']}: reverted")
            continue
        # Inside the hash-proven prefix no git call is needed. Beyond it a later
        # patch may have changed this patch's lines, so fall back to the exact
        # final hashes as apply-sdk-patches.ps1 does.
        applied = index < prefix or is_applied(path) or final_hashes_match(patch)
        if options.check:
            print(f"{patch['file']}: {'applied' if applied else 'NOT applied'}")
            bad += not applied
            continue
        if applied:
            if index >= prefix:
                print(f"{patch['file']}: already applied")
            continue
        if git("apply", "--check", str(path)) != 0:
            raise SystemExit(f"{patch['file']}: does not apply to the pinned sources (local edits?)")
        if git("apply", str(path)) != 0:
            raise SystemExit(f"{patch['file']}: apply failed")
        print(f"{patch['file']}: applied")
    if not options.check and not options.revert and prefix == len(patches):
        print(f"{len(patches)} patches already applied (hashes match deps.lock.json)")

    if not options.revert:
        # Final hashes are consistent per file (checked above): verify each once.
        final = {}
        for patch in patches:
            for name, digest in patch["files"].items():
                final[name] = (digest, patch["file"])
        for name, (digest, last) in final.items():
            actual = sha256(SDK / name)
            if actual != digest:
                print(f"{name}: SHA-256 {actual}, expected {digest} after {last}")
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
