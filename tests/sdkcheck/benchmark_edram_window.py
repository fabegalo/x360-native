"""Build an isolated 0197 control, then compare the real cache with 0198.

Run via scripts/devshell.cmd after building x360n_edram_window_checks Release.
The integrated SDK and lock are never modified. Outputs stay in .local.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--affinity", type=lambda n: int(n, 0), default=4)
    args = parser.parse_args()
    if not 1 <= args.trials <= 10:
        parser.error("trials must be 1..10")
    root = Path(__file__).resolve().parents[2]
    build = root / ".local/build/win-amd64"
    work = root / ".local/benchmarks/edram-window"
    before = work / "before"
    before.mkdir(parents=True, exist_ok=True)
    lock = json.loads((root / "deps.lock.json").read_text())
    entry = next(p for p in lock["rexglue_sdk"]["patches"]
                 if Path(p["file"]).name.startswith("0198-"))
    patch = root / entry["file"]
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha(patch.read_bytes()) == entry["sha256"]
    # Reconstruct precisely the pre-0198 files; never reverse the actual SDK.
    for rel, expected in entry["files"].items():
        data = (root / "external/rexglue-sdk" / rel).read_bytes()
        assert sha(data) == expected, f"SDK differs from 0198: {rel}"
        dest = before / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    subprocess.run(["git", "init", "-q", str(before)], check=True)
    subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "--reverse",
                    "--check", str(patch)], cwd=before, check=True)
    subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "--reverse",
                    str(patch)], cwd=before, check=True)
    for rel, expected in entry["base_files"].items():
        assert sha((before / rel).read_bytes()) == expected, rel
    target = "x360n_edram_window_checks"
    ninja = shutil.which("ninja")
    assert ninja, "use scripts/devshell.cmd"
    lines = subprocess.check_output([ninja, "-f", "build-Release.ninja", "-t",
                                     "commands", target], cwd=build, text=True).splitlines()
    link = lines[-1]
    sources = ["tests/sdkcheck/g3/edram_window_offsets.cpp"] + [
        "external/rexglue-sdk/" + p for p in (
            "src/graphics/util/draw.cpp", "src/graphics/util/draw_extent_estimator.cpp",
            "src/graphics/pipeline/render_target/cache.cpp")]
    for rel in sources:
        command = next(c for c in lines if c.endswith("/" + rel))
        obj = command.split(" -o ")[1].split(" -c ")[0]
        new_obj = (work / (Path(rel).stem + ".obj")).as_posix()
        link = link.replace(obj, new_obj)
        command = command.replace(" -o " + obj, " -o " + new_obj)
        if rel.startswith("external/"):
            command = command.replace((root / rel).as_posix(),
                                      (before / rel.removeprefix("external/rexglue-sdk/")).as_posix())
        command = command.replace(" -D", " -I" + (before / "include").as_posix() +
                                  " -DX360N_EDRAM_WINDOW_LEGACY -D", 1)
        subprocess.run(command, cwd=build, shell=True, check=True, timeout=120)
    control = "baseline-" + target
    link = link.replace("Release\\" + target + ".exe", "Release\\" + control + ".exe")
    subprocess.run(link, cwd=build, shell=True, check=True, timeout=120)
    # Child-only affinity, inherited by the probes; system settings stay intact.
    kernel = ctypes.windll.kernel32
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    assert kernel.SetProcessAffinityMask(kernel.GetCurrentProcess(), args.affinity)
    exe_dir = build / "tests/sdkcheck/g3/Release"
    for trial in range(args.trials):
        print(f"trial={trial+1}, affinity={args.affinity:#x}", flush=True)
        names = [control, target] if trial % 2 == 0 else [target, control]
        for name in names:
            print(name, flush=True)
            subprocess.run([str(exe_dir / (name + ".exe")), "--bench"],
                           check=True, timeout=30)


if __name__ == "__main__":
    main()
