"""MAINT-01 inventory and conservative, resumable compiler-cache cleanup.

No directory deletion, migration, age policy, evidence deletion or deduplication.
Python 3.11+, standard library only. local-workspace.ps1 is the public entrypoint.
"""
import argparse
from collections import Counter, defaultdict
import ctypes
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import stat
import subprocess
import sys
import time
from functools import lru_cache

SCHEMA = 1
COMPILER = {'.obj', '.o', '.pch', '.ilk', '.idb'}
TOOLS = {'x360-native.exe', 'x360n.exe', 'x360n-extract.exe'}
BUSY = re.compile(r'^(x360.*|rexglue|cmake|ninja|clang.*|cl|link|lld.*)(\.exe)?$', re.I)
PROTECTED = {'inputs', 'user', 'userdata', 'keys', 'catalog', 'toolchains',
             'worktrees', 'reference', 'packages', 'preparation'}


def norm(p):
    return os.path.normcase(os.path.abspath(p))


def native(p):
    text = os.path.abspath(p)
    return ('\\\\?\\' + text) if os.name == 'nt' and not text.startswith('\\\\?\\') else text


def info(p):
    return os.stat(native(p), follow_symlinks=False)


def beneath(p, parent):
    return norm(p) == norm(parent) or norm(p).startswith(norm(parent) + os.sep)


def redirected(s):
    return stat.S_ISLNK(s.st_mode) or bool(getattr(s, 'st_file_attributes', 0) & 0x400)


def safe_path(root, relative):
    p = Path(os.path.abspath(root / relative))
    if not beneath(p, root / '.local') or p == root / '.local':
        raise ValueError(f'Outside approved .local root: {p}')
    q = p
    while True:
        if redirected(info(q)):
            raise ValueError(f'Redirected path: {q}')
        if q == root:
            break
        q = q.parent
    # Workspace ancestors may themselves be junctions.
    for q in root.parents:
        if redirected(info(q)):
            raise ValueError(f'Redirected workspace ancestor: {q}')
    return p


def file_key(s):
    return f'{s.st_dev}:{s.st_ino}'


def digest(p):
    with open(native(p), 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def allocated(p, s):
    if os.name != 'nt':
        return getattr(s, 'st_blocks', 0) * 512
    k = storage_api()
    if not (getattr(s, 'st_file_attributes', 0) & (0x200 | 0x800)):
        class Standard(ctypes.Structure):
            _fields_ = [('allocation', ctypes.c_longlong), ('eof', ctypes.c_longlong),
                        ('links', W.DWORD), ('pending', ctypes.c_byte), ('directory', ctypes.c_byte)]
        h = k.CreateFileW(native(p), 0x80, 7, None, 3, 0x00200000, None)
        if h == W.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            value = Standard()
            if not k.GetFileInformationByHandleEx(h, 1, ctypes.byref(value), ctypes.sizeof(value)):
                raise ctypes.WinError(ctypes.get_last_error())
            return value.allocation
        finally:
            k.CloseHandle(h)
    hi = W.DWORD()
    k.GetCompressedFileSizeW.argtypes = [W.LPCWSTR, ctypes.POINTER(W.DWORD)]
    k.GetCompressedFileSizeW.restype = W.DWORD
    ctypes.set_last_error(0)
    lo = k.GetCompressedFileSizeW(native(p), ctypes.byref(hi))
    if lo == 0xffffffff and ctypes.get_last_error():
        raise ctypes.WinError(ctypes.get_last_error())
    return (hi.value << 32) | lo


@lru_cache(maxsize=1)
def storage_api():
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE]
    k.CreateFileW.restype = W.HANDLE
    k.GetFileInformationByHandleEx.argtypes = [W.HANDLE,ctypes.c_int,W.LPVOID,W.DWORD]
    k.CloseHandle.argtypes = [W.HANDLE]
    return k


def measure_allocation(root, out):
    root = Path(os.path.abspath(root)); out = Path(out)
    db = sqlite3.connect(out/'inventory.sqlite')
    total = logical = count = 0; physical = set()
    for rel,ident in list(db.execute('SELECT path,identity FROM files')):
        p = root/rel; s = info(p)
        if file_key(s) != ident:
            raise ValueError(f'Changed physical identity: {rel}')
        alloc = allocated(p,s)
        db.execute('UPDATE files SET allocated=? WHERE path=?',(alloc,rel))
        logical += s.st_size; count += 1
        if ident not in physical:
            total += alloc; physical.add(ident)
        if count % 50000 == 0:
            db.commit(); print(f'Allocation measured: {count} files',file=sys.stderr,flush=True)
    db.commit(); db.close()
    value = {'files':count,'logical_bytes':logical,'allocated_unique_bytes':total,
             'free_bytes':shutil.disk_usage(root).free,
             'method':'Win32 FileStandardInfo.AllocationSize; sparse/compressed GetCompressedFileSizeW; unique volume/file IDs; excludes filesystem metadata and report directory'}
    write_json(out/'allocation.json',value); print(json.dumps(value,indent=2))


@lru_cache(maxsize=None)
def compiler_graph(build):
    cache = build / 'CMakeCache.txt'
    if not os.path.isfile(native(cache)):
        return set(), []
    with open(native(cache), encoding='utf-8-sig') as f:
        text = f.read()
    home = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$', text, re.M)
    if not text.startswith('# This is the CMakeCache file.') or not home:
        return set(), []
    source = Path(home.group(1).strip())
    if not os.path.isfile(native(source / 'CMakeLists.txt')):
        return set(), []
    graph_paths = [build / 'build.ninja']
    graph_dir = build / 'CMakeFiles'
    if graph_dir.is_dir():
        graph_paths.extend(graph_dir.glob('impl-*.ninja'))
    outputs, references = set(), [cache]
    for graph in graph_paths:
        if not os.path.isfile(native(graph)):
            continue
        with open(native(graph), encoding='utf-8-sig') as f:
            text = f.read()
        if not text.startswith('# CMAKE generated file: DO NOT EDIT!'):
            continue
        references.append(graph)
        for output in re.findall(r'^build (.+?): ', text, re.M):
            output = output.split(' | ')[0].replace('$ ', ' ').replace('$:', ':').replace('$$', '$')
            if Path(output).suffix.lower() in COMPILER:
                outputs.add(norm(build / output))
    return outputs, references


def compiler_proof(root, rel):
    p = root / rel
    for parent in p.parents:
        if not beneath(parent, root / '.local' / 'build'):
            break
        outputs, refs = compiler_graph(parent)
        if norm(p) in outputs:
            return refs
    return []


def processes():
    if os.name == 'nt':
        # Toolhelp obtains a complete process-name snapshot without CIM/WMI.
        class Entry(ctypes.Structure):
            _fields_ = [('size', W.DWORD), ('usage', W.DWORD), ('pid', W.DWORD),
                        ('heap', ctypes.c_size_t), ('module', W.DWORD),
                        ('threads', W.DWORD), ('parent', W.DWORD),
                        ('priority', W.LONG), ('flags', W.DWORD), ('exe', W.WCHAR * 260)]
        k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.CreateToolhelp32Snapshot.restype = W.HANDLE
        k.Process32FirstW.argtypes = [W.HANDLE, ctypes.POINTER(Entry)]
        k.Process32NextW.argtypes = [W.HANDLE, ctypes.POINTER(Entry)]
        k.CloseHandle.argtypes = [W.HANDLE]
        h = k.CreateToolhelp32Snapshot(2, 0)
        if h == W.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        result = []
        try:
            e = Entry(); e.size = ctypes.sizeof(e)
            ok = k.Process32FirstW(h, ctypes.byref(e))
            if not ok:
                raise ctypes.WinError(ctypes.get_last_error())
            while ok:
                if e.pid != os.getpid() and BUSY.fullmatch(e.exe):
                    result.append({'pid': e.pid, 'name': e.exe})
                ok = k.Process32NextW(h, ctypes.byref(e))
        finally:
            k.CloseHandle(h)
        return result
    output = subprocess.check_output(['ps', '-eo', 'pid=,comm='], text=True)
    return [{'pid': int(line.split()[0]), 'name': line.split()[-1]} for line in output.splitlines()
            if BUSY.fullmatch(Path(line.split()[-1]).name)]


def walk(root, excluded=()):
    stack = [root]
    while stack:
        directory = stack.pop()
        with os.scandir(native(directory)) as entries:
            for e in entries:
                p = directory / e.name
                if any(beneath(p, x) for x in excluded):
                    continue
                # Windows DirEntry.stat may return zero inode/link count. Path.stat
                # queries the handle-backed identity needed for physical accounting.
                s = info(p)
                if redirected(s):
                    yield p, s, 'link'
                elif stat.S_ISDIR(s.st_mode):
                    stack.append(p)
                elif stat.S_ISREG(s.st_mode):
                    yield p, s, 'file'


def class_of(rel, worktrees=()):
    parts = Path(rel).parts
    low = [x.lower() for x in parts]
    if any(beneath(rel, w) for w in worktrees):
        return 'PROTECTED'
    if low[0] in {'.git', 'external'} or any(x in low for x in ('xerj', '.xerj')):
        return 'PROTECTED'
    if low[0] != '.local':
        return 'REQUIRED'
    if len(low) < 2 or low[1] in PROTECTED:
        return 'PROTECTED'
    if low[1] == 'evidence' and any(x.startswith('stable-') for x in low):
        return 'PROTECTED'
    if low[1] in {'evidence', 'archive'}:
        return 'HISTORICAL_UNIQUE'
    if low[1] == 'build':
        if 'cmakefiles' in low and Path(rel).suffix.lower() in COMPILER:
            return 'REGENERABLE'
        return 'REQUIRED'
    return 'UNKNOWN'


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for v in value.values():
            yield from strings(v)
    elif isinstance(value, list):
        for v in value:
            yield from strings(v)


def references(root, files):
    protected, blocked, sources = set(), [], {}
    setting = root / '.local/user-data-root.txt'
    if setting.is_file():
        sources[str(setting.relative_to(root))] = digest(setting)
        selected = setting.read_text(encoding='utf-8-sig').splitlines()[0].strip()
        protected.add(norm(selected if os.path.isabs(selected) else root / selected))
    for rel in files:
        p = root / rel
        if p.name.endswith('.preparation.json'):
            sha = digest(p); sources[rel] = sha
            scope = p.parent
            for parent in p.parents:
                if not beneath(parent, root / '.local'):
                    break
                if os.path.isfile(native(parent / 'CMakeCache.txt')):
                    scope = parent
                    break
            # Keep certified AOT trees fully warm, even if a structurally valid
            # historical receipt's external inputs can no longer be validated.
            blocked.append(str(scope))
            try:
                with open(native(p), encoding='utf-8-sig') as f:
                    data = json.load(f)
                for value in strings(data):
                    if os.path.isabs(value):
                        protected.add(norm(value))
                if data.get('schema') != 1 or not data.get('inputs') or not data.get('artifacts'):
                    raise ValueError('unknown/invalid preparation receipt')
            except (ValueError, TypeError, AttributeError):
                # Preserve the related CMake tree, including receipts kept in a
                # nested .preparation folder. Historical negative fixtures must
                # not globally disable cleanup of unrelated builds.
                scope = p.parent
                for parent in p.parents:
                    if not beneath(parent, root / '.local'):
                        break
                    if os.path.isfile(native(parent / 'CMakeCache.txt')):
                        scope = parent
                        break
                blocked.append(str(scope))
        elif p.suffix.lower() in {'.json', '.toml', '.md', '.sha256', '.txt', '.tsv'}:
            if p.name in {'compile_commands.json', 'CMakeCache.txt', 'TargetDirectories.txt'}:
                continue
            # All small manifests/reports, including unknown receipts. Raw logs and
            # Ninja dependency graphs describe intermediates; they do not certify them.
            if info(p).st_size > 8 * 1024 * 1024:
                if 'receipt' in p.name.lower() or 'manifest' in p.name.lower():
                    blocked.append(str(p.parent))
                continue
            try:
                with open(native(p), encoding='utf-8-sig') as f:
                    text = f.read()
            except UnicodeError:
                continue
            if 'CMakeFiles' in text or 'cmakefiles' in text:
                sources[rel] = digest(p)
                # Protect references by full/relative path and by object basename.
                for token in re.findall(r'[^\s"<>|,;]+\.(?:obj|pch|o|ilk|idb)', text, re.I):
                    token = token.replace('\\\\', '\\').replace('/', os.sep)
                    protected.add(norm(token if os.path.isabs(token) else root / token))
                    protected.add(Path(token).name.lower())
    return protected, blocked, sources


def eligible(root, rel, protected, blocked, worktrees):
    p = root / rel
    if class_of(rel) != 'REGENERABLE':
        return False
    if any(beneath(p, w) for w in worktrees + blocked):
        return False
    if norm(p) in protected or p.name.lower() in protected or any(norm(q) in protected for q in p.parents):
        return False
    return True


def refresh_plan(root, out):
    """Refine an existing metadata snapshot without another workspace traversal.

    Execution still re-discovers references and rechecks each identity and hash.
    """
    root = Path(os.path.abspath(root)); out = Path(out)
    with sqlite3.connect(out / 'inventory.sqlite') as db:
        rows = list(db.execute('SELECT * FROM files'))
    db.close()
    protected, blocked, refs = references(root, [r[0] for r in rows if r[6] != 'PROTECTED'])
    wt = worktree_paths(root); items = []
    for rel,size,mtime,ident,nlink,alloc,c,sha in rows:
        if nlink != 1 or not eligible(root,rel,protected,blocked,wt):
            continue
        proof = compiler_proof(root,rel)
        if not proof:
            continue
        s = info(root / rel)
        if (s.st_size,s.st_mtime_ns,file_key(s)) != (size,mtime,ident):
            raise ValueError(f'Inventory changed; re-audit: {rel}')
        sha = digest(root / rel)  # refreshed plan never trusts cached content
        for path in proof:
            refs[str(path.relative_to(root))] = digest(path)
        items.append({'path':rel,'size':size,'mtime':mtime,'identity':ident,'allocated':alloc,
                      'sha256':sha,'class':'REGENERABLE','proof':[str(p) for p in proof],
                      'reason':'CMake/Ninja-declared compiler intermediate; sources present; no certified/reference dependency. Next build recompiles this output.'})
    write_json(out/'plan.json',{'schema':SCHEMA,'workspace':str(root),'references':refs,'worktrees':wt,'items':items})
    print(json.dumps({'candidate_files':len(items),'candidate_bytes':sum(i['size'] for i in items),
                      'allocated_bytes':sum(i['allocated'] for i in items)},indent=2))


def worktree_paths(root):
    output = subprocess.check_output(['git', '-C', str(root), 'worktree', 'list', '--porcelain'], text=True)
    return [line[9:] for line in output.splitlines() if line.startswith('worktree ') and norm(line[9:]) != norm(root)]


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')


def audit(root, out, budget_gib=64, seconds=1200):
    start = time.monotonic(); deadline = start + seconds
    root = Path(os.path.abspath(root)); out = Path(os.path.abspath(out))
    if redirected(root.lstat()) or root.resolve() != root:
        raise ValueError('Workspace must be a real, unredirected absolute path')
    out.mkdir(parents=True, exist_ok=True)
    wt = worktree_paths(root)
    db = sqlite3.connect(out / 'inventory.sqlite')
    db.execute('CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY,size INTEGER,mtime INTEGER,identity TEXT,links INTEGER,allocated INTEGER,class TEXT,sha TEXT)')
    cache = {r[0]: r[1:] for r in db.execute('SELECT path,size,mtime,identity,sha FROM files WHERE sha IS NOT NULL')}
    db.execute('DELETE FROM files')
    totals = defaultdict(lambda: [0, 0, 0]); physical = set(); physical_bytes = 0; rows = []; links = []
    free_before = shutil.disk_usage(root).free
    for p, s, kind in walk(root, [out]):
        if time.monotonic() > deadline:
            raise TimeoutError('Inventory deadline; no cleanup performed')
        rel = str(p.relative_to(root))
        if kind == 'link':
            links.append({'path': rel, 'target': os.readlink(p) if p.is_symlink() else str(p.resolve())})
            continue
        ident = file_key(s); alloc = allocated(p, s)
        unique = ident not in physical; physical.add(ident)
        if unique:
            physical_bytes += alloc
        for depth in range(1, min(4, len(Path(rel).parts))):
            key = str(Path(*Path(rel).parts[:depth]))
            totals[key][0] += s.st_size; totals[key][1] += alloc if unique else 0; totals[key][2] += 1
        c = class_of(rel)
        if any(beneath(p, w) for w in wt):
            c = 'PROTECTED'
        old = cache.get(rel)
        sha = old[3] if old and old[:3] == (s.st_size, s.st_mtime_ns, ident) else None
        rows.append([rel, s.st_size, s.st_mtime_ns, ident, s.st_nlink, alloc, c, sha])
        if len(rows) % 25000 == 0:
            print(f'Inventoried {len(rows)} files', file=sys.stderr, flush=True)
    protected, blocked, refs = references(root, [r[0] for r in rows if r[6] != 'PROTECTED'])
    sizes = Counter(r[1] for r in rows if r[1] >= 1024 * 1024 and r[6] != 'PROTECTED')
    hashed = 0; deferred = 0; groups = defaultdict(list); plan = []; binaries = []
    for row in rows:
        rel, size, mtime, ident, nlink, alloc, c, sha = row
        candidate = eligible(root, rel, protected, blocked, wt) and nlink == 1
        proof = compiler_proof(root,rel) if candidate else []
        candidate = candidate and bool(proof)
        if candidate:
            for path in proof:
                key = str(path.relative_to(root))
                if key not in refs:
                    refs[key] = digest(path)
        binary = Path(rel).name.lower() in TOOLS or Path(rel).suffix.lower() == '.dll'
        need = candidate or binary or (c != 'PROTECTED' and sizes[size] > 1)
        if need and not sha:
            if hashed + size > budget_gib * 1024**3 or time.monotonic() > deadline:
                deferred += 1
            else:
                before = info(root / rel)
                sha = digest(root / rel); hashed += size
                after = info(root / rel)
                if (before.st_size, before.st_mtime_ns, file_key(before)) != (size, mtime, ident) or (after.st_size, after.st_mtime_ns, file_key(after)) != (size, mtime, ident):
                    raise ValueError(f'Changed during hashing: {rel}')
                row[7] = sha
        if sha:
            groups[(size, sha)].append({'path': rel, 'identity': ident, 'allocated': alloc, 'class': c})
        if binary:
            binaries.append({'path': rel, 'size': size, 'sha256': sha, 'class': c})
        if candidate and sha:
            plan.append({'path': rel, 'size': size, 'mtime': mtime, 'identity': ident,
                         'sha256': sha, 'allocated': alloc, 'class': c,
                         'reason': 'Unreferenced CMake compiler intermediate; next build recompiles it; final products and certified inputs retained.'})
    duplicates = [{'size': size, 'sha256': sha, 'files': members,
                   'distinct_physical_files': len({m['identity'] for m in members}),
                   'class': 'VERIFIED_DUPLICATE'}
                  for (size, sha), members in groups.items() if len(members) > 1]
    db.executemany('INSERT INTO files VALUES(?,?,?,?,?,?,?,?)', rows); db.commit(); db.close()
    summary = {'schema': SCHEMA, 'workspace': str(root), 'files': len(rows),
               'logical_bytes': sum(r[1] for r in rows),
               'allocated_unique_bytes': physical_bytes,
               'free_bytes': free_before, 'elapsed_seconds': round(time.monotonic()-start, 2),
               'hash_read_bytes': hashed, 'hash_deferred_files': deferred,
               'duplicates_groups': len(duplicates), 'duplicate_paths': sum(len(d['files'])-1 for d in duplicates),
               'reparse_points': links, 'directories': dict(sorted(totals.items(), key=lambda x: -x[1][0])),
               'blocked_receipt_roots': blocked, 'busy_processes': processes(),
               'candidate_files': len(plan), 'candidate_bytes': sum(r['size'] for r in plan),
               'candidate_allocated_bytes': sum(r['allocated'] for r in plan),
               'classification_counts': dict(Counter(r[6] for r in rows)),
               'scope_note': 'Report directory excluded; reparse targets never traversed. Allocation deduplicated by volume/file identity; not free-space prediction for hardlinks.'}
    write_json(out / 'summary.json', summary); write_json(out / 'duplicates.json', duplicates)
    write_json(out / 'binaries.json', binaries)
    write_json(out / 'plan.json', {'schema': SCHEMA, 'workspace': str(root), 'created': time.time(),
                                 'references': refs, 'worktrees': wt, 'items': plan})
    print(json.dumps({k:v for k,v in summary.items() if k not in {'directories','reparse_points'}}, indent=2))
    return summary


def remove_verified(root, item):
    p = safe_path(root, item['path'])
    if os.name != 'nt':
        raise RuntimeError('Deletion currently requires Windows exclusive delete handles')
    import msvcrt
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE]
    k.CreateFileW.restype = W.HANDLE
    k.SetFileInformationByHandle.argtypes = [W.HANDLE,ctypes.c_int,W.LPVOID,W.DWORD]
    k.GetFinalPathNameByHandleW.argtypes = [W.HANDLE,W.LPWSTR,W.DWORD,W.DWORD]
    h = k.CreateFileW(native(p), 0x80010000, 0, None, 3, 0x00200000, None)
    if h == W.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    fd = msvcrt.open_osfhandle(h, os.O_RDONLY | os.O_BINARY)
    with os.fdopen(fd, 'rb') as f:
        final = ctypes.create_unicode_buffer(32768)
        length = k.GetFinalPathNameByHandleW(h, final, len(final), 0)
        if not length or length >= len(final) or norm(final.value.removeprefix('\\\\?\\')) != norm(p):
            raise ValueError(f'Handle resolved outside planned path: {p}')
        s = os.fstat(f.fileno())
        if (s.st_size, s.st_mtime_ns, file_key(s), s.st_nlink) != (item['size'], item['mtime'], item['identity'], 1):
            raise ValueError(f'File identity/metadata changed: {p}')
        if hashlib.file_digest(f, 'sha256').hexdigest() != item['sha256']:
            raise ValueError(f'Content changed: {p}')
        yes = W.BOOL(1)
        if not k.SetFileInformationByHandle(h, 4, ctypes.byref(yes), ctypes.sizeof(yes)):
            raise ctypes.WinError(ctypes.get_last_error())


def execute(root, out, seconds=1200):
    root = Path(os.path.abspath(root))
    import msvcrt
    with open(safe_path(root, '.local/environment.lock'), 'a+b') as lease:
        lease.seek(0); msvcrt.locking(lease.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            return execute_locked(root, out, seconds)
        finally:
            lease.seek(0); msvcrt.locking(lease.fileno(), msvcrt.LK_UNLCK, 1)


def execute_locked(root, out, seconds=1200):
    root = Path(os.path.abspath(root)); out = Path(out)
    plan = json.loads((out / 'plan.json').read_text(encoding='utf-8'))
    if plan.get('schema') != SCHEMA or norm(plan['workspace']) != norm(root):
        raise ValueError('Plan belongs to another workspace/schema')
    if processes():
        raise RuntimeError(f'Active build/runtime processes; no deletion: {processes()}')
    wt = worktree_paths(root)
    # Discover new receipts as well as validate every reference in the plan.
    files = [str(p.relative_to(root)) for p,s,k in walk(root, [out]) if k == 'file'
             and p.suffix in {'.json','.toml','.md','.sha256','.txt','.tsv'}
             and class_of(str(p.relative_to(root))) != 'PROTECTED'
             and not any(beneath(p,w) for w in wt)]
    protected, blocked, refs = references(root, files)
    for rel, sha in plan['references'].items():
        if digest(root / rel) != sha:
            raise ValueError(f'Reference changed since dry-run: {rel}')
    lock_path = safe_path(root, '.local/environment.lock')
    import msvcrt
    with open(lock_path, 'a+b') as lock:
        before = shutil.disk_usage(root).free; removed = []; start = time.monotonic()
        journal_path = out / 'journal.jsonl'
        done = {}
        if journal_path.exists():
            for line in journal_path.read_text(encoding='utf-8').splitlines():
                entry = json.loads(line)
                if entry['status'] == 'removed':
                    done[entry['item']['path']] = entry['item']
        with journal_path.open('a', encoding='utf-8') as journal:
            def record(status, item, **extra):
                journal.write(json.dumps({'status': status, 'item': item, **extra})+'\n')
                journal.flush(); os.fsync(journal.fileno())
            try:
                for i, item in enumerate(plan['items']):
                    if time.monotonic()-start > seconds:
                        raise TimeoutError('Cleanup deadline; journal can be resumed')
                    if i % 32 == 0 and processes():
                        raise RuntimeError('Build/runtime started; cleanup interrupted')
                    rel = item['path']
                    if rel in done:
                        if (root / rel).exists():
                            raise ValueError(f'Previously deleted file recreated; create a new plan: {rel}')
                        continue
                    if not eligible(root, rel, protected, blocked, wt):
                        raise ValueError(f'Protected or referenced item: {rel}')
                    if not compiler_proof(root, rel):
                        raise ValueError(f'Not a declared CMake compiler output: {rel}')
                    record('intent', item)
                    try:
                        remove_verified(root, item)
                    except BaseException as e:
                        record('blocked', item, error=str(e)); raise
                    removed.append(item); record('removed', item)
            finally:
                after = shutil.disk_usage(root).free
                write_json(out / 'execution.json', {'removed_files_this_run': len(removed),
                    'removed_logical_bytes_this_run': sum(i['size'] for i in removed),
                    'removed_allocated_bytes_this_run': sum(i['allocated'] for i in removed),
                    'free_before': before, 'free_after': after, 'disk_free_delta': after-before,
                    'note': 'Free-space delta includes other concurrent disk activity. Each file rehashed under exclusive delete handle.'})
    print((out / 'execution.json').read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--refresh-plan', action='store_true')
    parser.add_argument('--measure-allocation', action='store_true')
    parser.add_argument('--hash-gib', type=float, default=64)
    parser.add_argument('--seconds', type=int, default=1200)
    args = parser.parse_args()
    if args.measure_allocation:
        measure_allocation(args.workspace,args.report)
    elif args.refresh_plan:
        refresh_plan(args.workspace, args.report)
    elif args.execute:
        execute(args.workspace, args.report, args.seconds)
    else:
        audit(args.workspace, args.report, args.hash_gib, args.seconds)
