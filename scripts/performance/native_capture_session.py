"""Capture real title frames with the Native renderer, validate and A/B them.

One command for a local session (e.g. Skate 3 on Windows/D3D12):

  1. starts the title with the Native renderer, the live frame profile and a
     frame capture armed (isolated copy of the user/cache seed);
  2. starts the capture automatically: after N presents (--skip-presents), N
     seconds after launch (--trigger-after) or when Enter is pressed in this
     console (--trigger-on-enter, for reaching a scene by hand);
  3. waits until the capture file is complete (header frame count), keeps the
     title running --profile-seconds longer for the live profile, then stops it;
  4. replays the capture (--replay-repeat passes) and requires it to be
     equivalent, repeatable and visually checked on every frame;
  5. optionally (--ab) runs native_replay_ab.py over submission strategies on
     the same frames and prints the per-arm percentiles and verdicts.

  python scripts/performance/native_capture_session.py --exe .local/build/titles/skate_3/skate_3.exe
      --game-data-root .local/inputs/skate_3 --user-seed .local/evidence/benchmarks/seeds/skate_3
      --out .local/captures/skate3/street-1 --trigger-on-enter --frames 3 --ab

Everything written (capture, logs, profile, reports) stays under --out, which
must be inside .local/ (captures hold title data) and must not exist yet.
session.json records the arguments and SHA-256 of the executable, its DLLs
and the capture. Exit 0: capture complete and replay equivalent; 3: replay
not equivalent; 2: capture rejected/incomplete; 1: other errors.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER_FRAMES_OFFSET = 8 + 4 * 4 + 16  # magic, version/register_count/scale_x/scale_y, backend
STRATEGIES = [('off', ['native_eager_submit=0']),
              ('idle64', ['native_eager_submit=64', 'native_eager_submit_depth=0']),
              ('queued64', ['native_eager_submit=64', 'native_eager_submit_depth=1']),
              ('idle256', ['native_eager_submit=256', 'native_eager_submit_depth=0']),
              ('queued256', ['native_eager_submit=256', 'native_eager_submit_depth=1'])]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def captured_frames(path: Path):
    """Frame count in the capture header: 0 while recording, N once finished."""
    try:
        with path.open('rb') as handle:
            handle.seek(HEADER_FRAMES_OFFSET)
            data = handle.read(4)
        return struct.unpack('<I', data)[0] if len(data) == 4 else None
    except OSError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', required=True, type=Path, help='title executable built with the Native plugin')
    parser.add_argument('--out', required=True, type=Path, help='new directory under .local/')
    parser.add_argument('--game-data-root', type=Path)
    parser.add_argument('--user-seed', type=Path, help='frozen user data copied into the session (never modified)')
    parser.add_argument('--api', choices=['d3d12', 'vulkan'], default='d3d12')
    parser.add_argument('--frames', type=int, default=3)
    start = parser.add_mutually_exclusive_group(required=True)
    start.add_argument('--skip-presents', type=int, help='start after this many presents (fully automatic)')
    start.add_argument('--trigger-after', type=float, help='start this many seconds after launch')
    start.add_argument('--trigger-on-enter', action='store_true', help='start when Enter is pressed here')
    parser.add_argument('--profile-seconds', type=float, default=10, help='live profile kept after the capture')
    parser.add_argument('--timeout', type=float, default=900, help='limit for the whole title session')
    parser.add_argument('--title-arg', action='append', default=[], help='extra title argument (e.g. --mnk_mode=true)')
    parser.add_argument('--replay', type=Path, help='x360n_native_replay(.exe) directory (default: this build)')
    parser.add_argument('--replay-repeat', type=int, default=10)
    parser.add_argument('--ab', action='store_true', help='A/B the submission strategies on the captured frames')
    parser.add_argument('--ab-rounds', type=int, default=7)
    args = parser.parse_args()

    out = args.out.resolve()
    local = (ROOT / '.local').resolve()
    if local not in out.parents:
        parser.error(f'--out must be inside {local} (captures hold title data)')
    if out.exists():
        parser.error(f'refusing to reuse {out}')
    if not args.exe.is_file():
        parser.error(f'missing {args.exe}')
    if args.frames < 1 or args.frames > 64:
        parser.error('--frames must be in [1, 64]')
    replay_dir = args.replay or ROOT / ('.local/build/win-amd64/src/native/Release' if os.name == 'nt'
                                        else '.local/build/linux-amd64/src/native/Release')
    replay_exe = replay_dir / ('x360n_native_replay.exe' if os.name == 'nt' else 'x360n_native_replay')
    if not replay_exe.is_file():
        parser.error(f'missing {replay_exe} (build x360n_native_replay or pass --replay)')

    out.mkdir(parents=True)
    user = out / 'user'
    if args.user_seed:
        shutil.copytree(args.user_seed, user)
    else:
        user.mkdir()
    capture = out / 'frames.x360nfc'
    trigger = out / 'go'
    title_args = [f'--gpu_plugin=native', f'--gpu_backend={args.api}', '--log_level=info',
                  f'--log_file={out / "rex.log"}', f'--user_data_root={user}', f'--cache_root={user / "cache"}',
                  '--native_stats=true', f'--native_frame_profile={out / "live-frames.csv"}',
                  f'--native_capture={capture}', f'--native_capture_frames={args.frames}']
    if args.game_data_root:
        title_args.append(f'--game_data_root={args.game_data_root.resolve()}')
    if args.skip_presents is not None:
        title_args.append(f'--native_capture_skip={args.skip_presents}')
    else:
        title_args.append(f'--native_capture_trigger={trigger}')
    title_args += args.title_arg
    session = {'schema': 1, 'utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'host': platform.platform(),
               'exe': str(args.exe), 'arguments': title_args,
               'artifacts': {p.name: sha256(p) for p in sorted(args.exe.parent.iterdir())
                             if p.is_file() and p.suffix.lower() in ('.exe', '.dll', '.so', '.toml')}}

    print(f'starting {args.exe.name} ({args.api}); capture of {args.frames} frame(s) to {capture}')
    title = subprocess.Popen([str(args.exe)] + title_args, cwd=args.exe.parent,
                             stdout=(out / 'stdout.log').open('w'), stderr=(out / 'stderr.log').open('w'))
    launched = time.monotonic()
    if args.trigger_on_enter:
        def wait_for_enter():
            input('Reach the scene, then press Enter to capture... ')
            trigger.touch()
        threading.Thread(target=wait_for_enter, daemon=True).start()
    outcome, finished_at = None, None
    try:
        while time.monotonic() - launched < args.timeout:
            elapsed = time.monotonic() - launched
            if args.trigger_after is not None and elapsed >= args.trigger_after and not trigger.exists():
                trigger.touch()
                print(f'capture triggered at {elapsed:.1f} s')
            if title.poll() is not None:
                outcome = outcome or f'title exited ({title.returncode}) before the capture completed'
                break
            frames = captured_frames(capture)
            if frames == args.frames and finished_at is None:
                finished_at = time.monotonic()
                print(f'capture complete ({capture.stat().st_size >> 20} MiB); profiling {args.profile_seconds:.0f} s more')
            if finished_at is not None and time.monotonic() - finished_at >= args.profile_seconds:
                outcome = 'complete'
                break
            if trigger.exists() and not capture.exists() and elapsed > 5 and finished_at is None and \
                    (out / 'rex.log').exists() and 'capture rejected' in (out / 'rex.log').read_text(errors='ignore'):
                outcome = 'capture rejected (see rex.log)'
                break
            time.sleep(0.25)
        else:
            outcome = 'session timeout'
    finally:
        if title.poll() is None:
            title.terminate()
            try:
                title.wait(15)
            except subprocess.TimeoutExpired:
                title.kill()
                title.wait()
    session['capture_outcome'] = outcome
    if outcome != 'complete':
        (out / 'session.json').write_text(json.dumps(session, indent=2) + '\n')
        print(f'capture failed: {outcome}', file=sys.stderr)
        return 2
    session['capture'] = {'file': capture.name, 'bytes': capture.stat().st_size, 'sha256': sha256(capture),
                          'frames': args.frames}

    print('replaying the capture and checking equivalence...')
    replay = subprocess.run([str(replay_exe), str(capture), str(out / 'replay'), '--backend', args.api,
                             '--repeat', str(args.replay_repeat), '--stats'], capture_output=True, text=True)
    report_path = out / 'replay' / 'replay.json'
    report = json.loads(report_path.read_text()) if report_path.exists() else None
    session['replay'] = {'exit': replay.returncode, 'report': report}
    if replay.returncode or not report or not report.get('visual_checked'):
        (out / 'session.json').write_text(json.dumps(session, indent=2) + '\n')
        print(f'replay not equivalent or failed ({replay.returncode}): {(report or {}).get("mismatches")}'
              f'\n{replay.stderr[-600:]}', file=sys.stderr)
        return 3 if replay.returncode == 3 else 1
    frame = report['per_frame']
    print(f'replay equivalent: warm frame P50/P95/P99 {report["warm_frame_ms_p50"]}/{report["warm_frame_ms_p95"]}/'
          f'{report["warm_frame_ms_p99"]} ms; draws {frame["draws"]}, resolves {frame["resolves"]}, submissions '
          f'{frame["submissions"]}, fence {frame["fence_wait_ms"]} ms, GPU busy {frame["gpu_busy_ms"]} ms, '
          f'bottleneck {frame["bottleneck"]}')

    if args.ab:
        command = [sys.executable, str(ROOT / 'scripts/performance/native_replay_ab.py'), '--capture', str(capture),
                   '--output', str(out / 'ab-submission.json'), '--backend', args.api,
                   '--rounds', str(args.ab_rounds), '--repeat', str(args.replay_repeat)]
        for name, cvars in STRATEGIES:
            command += ['--arm', name, str(replay_dir)] + cvars
        ab = subprocess.run(command)
        session['ab'] = {'exit': ab.returncode, 'report': 'ab-submission.json'}
        if ab.returncode:
            (out / 'session.json').write_text(json.dumps(session, indent=2) + '\n')
            return 3 if ab.returncode == 3 else 1
    (out / 'session.json').write_text(json.dumps(session, indent=2) + '\n')
    print(f'session written to {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
