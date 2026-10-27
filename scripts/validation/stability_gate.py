"""STABLE-01: bounded Release regression runner and fail-closed evidence verifier.

No original content is launched. XERJ and content remain local. The regression
result and the stable certification decision are deliberately separate.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / 'docs/stability/criteria.json'
STATES = {'PASS', 'FAIL', 'KNOWN FAILURE', 'SKIP', 'NOT RUN'}


def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def artifact(path):
    path = Path(path).resolve()
    return {'path': str(path), 'sha256': digest(path)}


def intact(item):
    return Path(item['path']).is_file() and digest(item['path']) == item['sha256']


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def command(args, cwd, log, timeout):
    env = os.environ.copy()
    # pwsh's inherited module path can hide Windows PowerShell's built-in
    # Get-FileHash. Use the Windows built-in modules for the child commands.
    windows = Path(os.environ.get('SystemRoot', 'C:/Windows'))
    env['PSMODULEPATH'] = str(windows / 'System32/WindowsPowerShell/v1.0/Modules')
    with Path(log).open('w', encoding='utf-8') as f:
        f.write(json.dumps({'command': [str(x) for x in args], 'cwd': str(cwd), 'timeout_seconds': timeout}) + '\n')
        f.flush()
        proc = subprocess.Popen([str(x) for x in args], cwd=cwd, stdout=f,
                                stderr=subprocess.STDOUT, env=env,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            return proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run([str(windows / 'System32/taskkill.exe'),
                                '/PID', str(proc.pid), '/T', '/F'], stdout=f,
                               stderr=subprocess.STDOUT, timeout=15,
                               creationflags=subprocess.CREATE_NO_WINDOW)
            else:
                proc.kill()
            proc.wait(timeout=15)
            f.write('\nFAIL: command exceeded bounded timeout\n')
            return 124


def junit(path, expected):
    """Exact set equality prevents empty, omitted and duplicate test success."""
    tree = ET.parse(path)
    cases = list(tree.iter('testcase'))
    names = [x.attrib['name'] for x in cases]
    if len(names) != len(set(names)) or set(names) != set(expected):
        raise ValueError('JUnit required test set differs (missing/extra/duplicate)')
    return [{'id': x.attrib['name'], 'state':
             'SKIP' if x.find('skipped') is not None else
             'FAIL' if x.find('failure') is not None or x.find('error') is not None else 'PASS'}
            for x in cases]


def reuse_contracts(previous, current, policy, build):
    """Reuse only unchanged main/SDK evidence; preserve every failure/skip."""
    if previous.get('identity') != current['identity'] or previous.get('preflight') != 'PASS':
        raise ValueError('reused contracts have a different identity/preflight')
    def baseline(items):
        return {x['path']: x['sha256'] for x in items
                if Path(x['path']).is_relative_to(build) or x.get('role') in ('sdk-source', 'sdk-patch', 'lock')}
    if not baseline(current['artifacts']) or baseline(previous['artifacts']) != baseline(current['artifacts']):
        raise ValueError('reused main/SDK artifact set changed')
    for item in previous['artifacts']:
        if item['path'] in baseline(current['artifacts']) and not intact(item):
            raise ValueError('reused main/SDK artifact changed or disappeared')
    xml = previous['junit']['contracts']
    if not intact(xml):
        raise ValueError('reused JUnit changed or disappeared')
    rows = junit(xml['path'], policy['tests'])
    for row in rows:
        recorded = [x for x in previous['results'] if x['id'] == row['id']]
        if len(recorded) != 1 or recorded[0]['state'] != row['state'] or not recorded[0].get('evidence') or not all(intact(x) for x in recorded[0]['evidence']):
            raise ValueError('reused result/evidence mismatch')
    return rows, xml


def verify(report, policy):
    errors = []
    if report.get('schema') != 1 or report.get('configuration') != 'Windows-x64/Release/Xenos/D3D12/17559':
        errors.append('invalid schema/configuration')
    identity = report.get('identity', {})
    for key, size in [('commit', 40), ('sdk_commit', 40), ('lock_sha256', 64), ('policy_sha256', 64)]:
        if not re.fullmatch('[0-9a-f]{%d}' % size, identity.get(key, '')):
            errors.append('missing identity: ' + key)
    if identity.get('policy_sha256') != digest(POLICY):
        errors.append('policy identity changed')
    if report.get('preflight') != 'PASS':
        errors.append('SDK/build preflight failed')
    if report.get('reused_contracts') and not intact(report['reused_contracts']):
        errors.append('reused report missing/changed')
    source = Path(report.get('source', 'missing-source'))
    if not source.is_dir() or git(source, 'rev-parse', 'HEAD') != identity.get('commit'):
        errors.append('source commit changed/absent')
    elif digest(source / 'deps.lock.json') != identity.get('lock_sha256'):
        errors.append('lock identity changed')
    artifacts_by_path = {x['path']: x['sha256'] for x in report.get('artifacts', [])}
    for folder in report.get('artifact_directories', []):
        for path in Path(folder).glob('*'):
            if path.suffix.lower() in ('.exe', '.dll') and str(path.resolve()) not in artifacts_by_path:
                errors.append('unrecorded/omitted binary: ' + str(path))
    artifacts = report.get('artifacts', [])
    if not artifacts or not all(intact(x) for x in artifacts):
        errors.append('missing/changed artifact')
    required_artifacts = set(policy['artifact_roles'])
    if not required_artifacts.issubset({x.get('role') for x in artifacts}):
        errors.append('required artifact role absent')
    if not report.get('preflight_evidence') or not all(intact(x) for x in report['preflight_evidence']):
        errors.append('SDK/build evidence absent/changed')
    rows = report.get('results', [])
    required = set(policy['tests']) | set(policy['manual']) | {'desktop.catalog', 'desktop.update-recovery', 'synthetic-recovery'}
    ids = [x['id'] for x in rows]
    if len(ids) != len(set(ids)) or set(ids) != required:
        errors.append('required result set differs')
    for row in rows:
        if row.get('state') not in STATES or row.get('state') != 'PASS':
            errors.append(row['id'] + ': ' + str(row.get('state')))
        elif not row.get('evidence') or not all(intact(x) for x in row['evidence']):
            errors.append(row['id'] + ': evidence missing/changed')
        if row['id'] in policy['manual'] and row.get('state') == 'PASS':
            # Human evidence must bind originals, title version, profile, HDD,
            # artifacts and observed outcome. Hashes never prove gameplay.
            if row.get('identity') != identity or not row.get('content') or not row.get('observation'):
                errors.append(row['id'] + ': unbound original-content observation')
            for content in row.get('content', []):
                if not intact(content):
                    errors.append(row['id'] + ': original identity changed')
    # Reparse evidence, rather than trusting a hand-edited PASS field.
    for label, expected in [('contracts', policy['tests']), ('desktop', ['desktop.catalog', 'desktop.update-recovery']),
                            ('synthetic', ['stable session crash and relaunch preserve the single HDD bytes'])]:
        item = report.get('junit', {}).get(label)
        if not item or not intact(item):
            errors.append(label + ': JUnit missing/changed')
            continue
        actual = junit(item['path'], expected)
        for row in actual:
            name = 'synthetic-recovery' if label == 'synthetic' else row['id']
            recorded = next((x for x in rows if x['id'] == name), {})
            if row['state'] != recorded.get('state'):
                errors.append(name + ': result differs from JUnit')
    return errors


def run(args):
    policy = json.loads(POLICY.read_text())
    repo, build, desktop = args.source.resolve(), args.build.resolve(), args.desktop.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)  # never overwrite old evidence
    report = {'schema': 1, 'configuration': 'Windows-x64/Release/Xenos/D3D12/17559',
              'started_utc': datetime.now(timezone.utc).isoformat(),
              'source': str(repo),
              'identity': {'commit': git(repo, 'rev-parse', 'HEAD'),
                           'sdk_commit': git(repo / 'external/rexglue-sdk', 'rev-parse', 'HEAD'),
                           'lock_sha256': digest(repo / 'deps.lock.json'),
                           'policy_sha256': digest(POLICY)},
              'preflight': 'FAIL', 'artifacts': [], 'results': []}
    report['candidate_commit'] = git(ROOT, 'rev-parse', 'HEAD')
    try:
        if platform.system() != 'Windows' or platform.machine().lower() not in ('amd64', 'x86_64'):
            raise ValueError('Windows x64 required')
        for cache, source in [(build, repo), (desktop, ROOT)]:
            text = (cache / 'CMakeCache.txt').read_text()
            if f'CMAKE_HOME_DIRECTORY:INTERNAL={source.as_posix()}' not in text:
                raise ValueError('build belongs to another source directory')
            if 'Release' not in text or (cache == build and 'REXGLUE_USE_D3D12:BOOL=ON' not in text):
                raise ValueError('Release/D3D12 configuration absent')
        if git(repo, 'status', '--porcelain', '--untracked-files=no', '--', 'src', 'tests', 'cmake', 'CMakeLists.txt', 'deps.lock.json', 'patches'):
            raise ValueError('baseline runtime sources are dirty')
        lock = json.loads((repo / 'deps.lock.json').read_text())
        # Git may check this JSON out as CRLF in the isolated worktree. Preserve
        # both byte identities; compare the parsed lock across checkouts only.
        if report['identity']['sdk_commit'] != lock['rexglue_sdk']['commit'] or json.loads((ROOT / 'deps.lock.json').read_text()) != lock:
            raise ValueError('SDK/lock identity mismatch')
        dev = repo / 'scripts/devshell.cmd'
        steps = [(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', repo / 'scripts/setup/apply-sdk-patches.ps1', '-Check'], 'sdk', 180),
                 ([dev, 'cmake', '--build', build, '--config', 'Release', '--target', 'x360n_tests', 'x360n_sdk_runtime_checks'], 'build-main', 600),
                 ([dev, 'cmake', '--build', desktop, '--config', 'Release', '--target', 'x360n_launcher_tests', 'x360n_desktop_tests'], 'build-candidate', 600)]
        steps.append(([sys.executable, '-m', 'unittest', 'discover', '-s', ROOT / 'tests/stability', '-v'], 'negative-controls', 90))
        for cmd, name, limit in steps:
            if command(cmd, repo, out / (name + '.log'), limit):
                raise ValueError(name + ' failed; no tests run on stale binaries')
        report['preflight_evidence'] = [artifact(out / (name + '.log')) for name in ['sdk', 'build-main', 'build-candidate', 'negative-controls']]
        ctest = repo / '.local/toolchains/cmake-3.31.6/bin/ctest.exe'
        exe = build / 'tests/Release'
        roles = {'supervisor-tests': exe / 'x360n_tests.exe',
                 'supervisor': build / 'src/supervisor/Release/x360n.exe',
                 'runtime-tests': exe / 'x360n_sdk_runtime_checks.exe',
                 'runtime': exe / 'rexruntime.dll', 'xenos': exe / 'rexgpu-xenos.dll',
                 'desktop-tests': desktop / 'src/desktop/x360n_desktop_tests.exe',
                 'synthetic-tests': desktop / 'src/desktop/x360n_launcher_tests.exe',
                 'lock': repo / 'deps.lock.json', 'policy': POLICY,
                 'gate': Path(__file__).resolve()}
        for role, path in roles.items():
            report['artifacts'].append(dict(artifact(path), role=role))
        # All sibling native binaries/DLLs and children used by these tests are bound.
        folders = [exe, build / 'src/supervisor/Release', build / 'src/content/Release',
                   desktop / 'src/desktop', desktop / 'src/supervisor', desktop / 'src/content']
        for folder in folders:
            for path in sorted(folder.glob('*')):
                if path.suffix.lower() in ('.exe', '.dll'):
                    report['artifacts'].append(dict(artifact(path), role='dependency'))
        report['artifact_directories'] = [str(x) for x in folders]
        # Final expected hashes incorporate successive patches to the same file.
        sdk_files = {}
        for patch in lock['rexglue_sdk']['patches']:
            sdk_files.update(patch['files'])
            report['artifacts'].append(dict(artifact(repo / patch['file']), role='sdk-patch'))
        for name, expected in sdk_files.items():
            path = repo / 'external/rexglue-sdk' / name
            if digest(path) != expected:
                raise ValueError('SDK final file mismatch: ' + name)
            report['artifacts'].append(dict(artifact(path), role='sdk-source'))
        report['preflight'] = 'PASS'
        pattern = '^(' + '|'.join(re.escape(x) for x in policy['tests']) + ')$'
        for folder, names, regex, label in [(build, policy['tests'], pattern, 'contracts'),
                (desktop, ['desktop.catalog', 'desktop.update-recovery'], '^desktop[.](catalog|update-recovery)$', 'desktop')]:
            xml = out / (label + '.xml')
            if label == 'contracts' and args.reuse_contracts:
                previous = json.loads(args.reuse_contracts.read_text())
                rows, item = reuse_contracts(previous, report, policy, build)
                xml = Path(item['path'])
                (out / (label + '.log')).write_text(json.dumps({'reused_from': artifact(args.reuse_contracts),
                    'reason': 'unchanged main/SDK artifacts; failures preserved'}), encoding='utf-8')
                report['reused_contracts'] = artifact(args.reuse_contracts)
                rc = 0
            else:
                rc = command([ctest, '--test-dir', folder, '-C', 'Release', '-R', regex,
                              '--output-junit', xml, '--output-on-failure', '--timeout', '180'], repo, out / (label + '.log'), 600)
                rows = junit(xml, names)
            report.setdefault('junit', {})[label] = artifact(xml)
            if rc and all(x['state'] == 'PASS' for x in rows):
                raise ValueError('CTest command failed despite PASS rows')
            for row in rows:
                row['evidence'] = [artifact(xml), artifact(out / (label + '.log'))]
            report['results'].extend(rows)
        xml = out / 'synthetic.xml'
        rc = command([roles['synthetic-tests'], '[stability]', '--reporter', 'junit', '--out', xml],
                     out, out / 'synthetic.log', 90)
        rows = junit(xml, ['stable session crash and relaunch preserve the single HDD bytes'])
        report['junit']['synthetic'] = artifact(xml)
        report['results'].append({'id': 'synthetic-recovery', 'state': rows[0]['state'] if rc == 0 else 'FAIL',
                                  'evidence': [artifact(xml), artifact(out / 'synthetic.log')]})
    except (ValueError, OSError, subprocess.SubprocessError, ET.ParseError) as e:
        report['error'] = str(e)
    for name in policy['manual']:
        report['results'].append({'id': name, 'state': 'NOT RUN', 'evidence': []})
    errors = verify(report, policy)
    report['certified'] = not errors
    report['blockers'] = errors
    (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'certified': not errors, 'results': len(report['results']), 'blockers': errors}, indent=2))
    return 1 if errors else 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    r = sub.add_parser('run')
    for name in ['source', 'build', 'desktop', 'output']:
        r.add_argument('--' + name, type=Path, required=True)
    r.add_argument('--reuse-contracts', type=Path,
                   help='reuse intact main/SDK results from a previous report, including failures')
    v = sub.add_parser('verify')
    v.add_argument('report', type=Path)
    args = p.parse_args()
    if args.action == 'run':
        return run(args)
    try:
        errors = verify(json.loads(args.report.read_text()), json.loads(POLICY.read_text()))
    except (OSError, ValueError, KeyError, TypeError) as e:
        errors = [str(e)]
    print('\n'.join(errors) if errors else 'PASS')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
