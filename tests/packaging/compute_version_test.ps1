# Replays a synthetic history through compute-version.ps1; no network, no project history.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script = Join-Path $root 'scripts/packaging/compute-version.ps1'
$repo = Join-Path ([IO.Path]::GetTempPath()) ('x360n-version-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $repo | Out-Null
try {
    git -C $repo init -q
    function Commit([string]$message, [string]$expected) {
        $file = Join-Path $repo 'message.txt'
        [IO.File]::WriteAllText($file, $message + "`n")
        git -C $repo -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false commit -q --allow-empty -F $file
        if ($LASTEXITCODE) { throw 'commit failed' }
        $version = & $script -Repository $repo
        if ($version -ne $expected) { throw "After '$($message.Split("`n")[0])': expected $expected, got $version" }
    }
    Commit 'chore: initial layout' '0.0.0'
    Commit 'feat(core): first feature' '0.1.0'
    Commit 'fix: repair' '0.1.1'
    Commit 'perf(launch): faster' '0.1.2'
    Commit 'docs: notes' '0.1.2'
    Commit 'Update readme' '0.1.2'
    Commit 'fix:missing space is not conventional' '0.1.2'
    Commit 'Feat: wrong case is not conventional' '0.1.2'
    Commit 'feat: second feature' '0.2.0'
    Commit 'fix(ui): after feature' '0.2.1'
    Commit 'feat(api)!: breaking while major is zero' '0.3.0'
    Commit "refactor: footer breaking`n`nBREAKING CHANGE: changed contract" '0.4.0'
    Commit "chore: stabilize`n`nRelease-As: 1.0.0" '1.0.0'
    Commit 'fix: after stable' '1.0.1'
    Commit 'feat!: breaking after stable' '2.0.0'
    Commit "test: footer mention`n`nmentions BREAKING CHANGE: inline only" '2.0.0'
    # A merge replays the commits of both parents once.
    git -C $repo checkout -q -b side
    Commit 'feat: on side branch' '2.1.0'
    git -C $repo checkout -q -
    Commit 'fix: on main' '2.0.1'
    git -C $repo -c user.name=test -c user.email=test@example.invalid merge -q --no-ff side -m "Merge branch 'side'"
    $merged = & $script -Repository $repo
    if ($merged -notin @('2.1.1', '2.1.0')) { throw "Merge replay produced $merged" }
    $file = Join-Path $repo 'message.txt'
    [IO.File]::WriteAllText($file, "chore: go back`n`nRelease-As: 1.5.0`n")
    git -C $repo -c user.name=test -c user.email=test@example.invalid commit -q --allow-empty -F $file
    $failed = $false
    try { & $script -Repository $repo | Out-Null } catch { $failed = $_.Exception.Message -match 'does not advance' }
    if (-not $failed) { throw 'Release-As accepted a lower version' }
    'PASS: feat/fix/perf/breaking/Release-As rules, non-conventional subjects ignored, merges replayed'
} finally {
    Remove-Item -LiteralPath $repo -Recurse -Force
}
