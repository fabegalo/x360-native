# Builds a title project under titles/<name> from scratch, including codegen.
#
# With the v0.10.0 init template in REXSDK_DIR mode the rexglue tool is built in
# the same tree, and generated/<module>/sources.cmake is only included if it
# exists at configure time (generated/rexglue.cmake). A single configure+build
# on a tree without generated code therefore runs codegen but links without the
# guest code ("undefined symbol ... PPCImageConfig"), and rebuilding does not
# re-run CMake. Hence four steps:
#   1 configure   2 build <name>_codegen   3 configure again   4 build
# Each step's output goes to <BuildDir>/x360n-build-<step>.log; stops at the first failure.
# Usage: scripts/build-title.ps1 -Title iruka [-BuildDir .local/build/titles/iruka]
param(
    [Parameter(Mandatory)] [string]$Title,
    [string]$BuildDir
)
$ErrorActionPreference = 'Stop'
# Preparation uses only modules shipped with the selected PowerShell host.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
if ($Title -notmatch '^[a-z][a-z0-9_]*$') { throw "invalid title name '$Title'" }
$titleDir = Join-Path $root "titles\$Title"
if (-not (Test-Path (Join-Path $titleDir 'CMakePresets.json'))) { throw "no title project at $titleDir" }
if (-not $BuildDir) { $BuildDir = ".local/build/titles/$Title" }
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
New-Item -ItemType Directory -Force $build | Out-Null
$leasePath = Join-Path $root '.local/environment.lock'
try { $lease = [IO.File]::Open($leasePath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
catch { throw 'Environment in use. Close the platform or other preparation and retry. Ambiente em uso; feche a plataforma e tente novamente.' }
$marker = Join-Path $root ".local/preparation/$Title.incomplete"
New-Item -ItemType Directory -Force (Split-Path $marker) | Out-Null
[IO.File]::WriteAllText($marker, 'incomplete')
$devshell = Join-Path $root 'scripts\devshell.cmd'

$steps = @(
    @{ name = '1-configure';  args = "cmake --preset x360n-win-amd64 -B `"$build`"" },
    @{ name = '2-codegen';    args = "cmake --build `"$build`" --target ${Title}_codegen" },
    @{ name = '3-reconfigure'; args = "cmake --preset x360n-win-amd64 -B `"$build`"" },
    @{ name = '4-build';      args = "cmake --build `"$build`"" }
)
Push-Location $titleDir
try {
    foreach ($s in $steps) {
        Write-Output "X360N_STAGE $Title $($s.name)"
        $log = Join-Path $build "x360n-build-$($s.name).log"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # Redirect inside cmd: in PowerShell 5.1 with ErrorActionPreference=Stop,
        # native stderr (e.g. VsDevCmd's "'vswhere.exe' is not recognized") would
        # otherwise become a terminating NativeCommandError.
        cmd /c "`"$devshell`" $($s.args) > `"$log`" 2>&1"
        $rc = $LASTEXITCODE
        Write-Host ("step {0}: exit {1} in {2} s (log {3})" -f $s.name, $rc, [int]$sw.Elapsed.TotalSeconds, $log)
        if ($rc -ne 0) {
            Get-Content $log | Select-String -Pattern 'error|FAILED' | Select-Object -First 10 | Out-Host
            exit $rc
        }
        if ($s.name -in @('1-configure', '3-reconfigure')) {
            # CMake 3.31's __create_def calls CreateFileW without converting long
            # object paths (Source/bindexplib.cxx). Keep its generated object list
            # and symbol extraction; only spell paths >= MAX_PATH in extended form.
            # Configure regenerates these lists, so repeat after both configure steps.
            # Use CMake's target list: PowerShell 5's recursive directory walk
            # also rejects the long object directories that caused this failure.
            foreach ($directory in [IO.File]::ReadAllLines((Join-Path $build 'CMakeFiles/TargetDirectories.txt'))) {
                $list = Join-Path $directory 'exports.def.objs'
                if (-not [IO.File]::Exists($list)) { continue }
                $changed = $false
                $entries = foreach ($entry in [IO.File]::ReadAllLines($list)) {
                    if (-not $entry -or $entry.StartsWith('\\?\')) { $entry; continue }
                    $absolute = [IO.Path]::GetFullPath((Join-Path $build $entry))
                    if ($absolute.Length -lt 260) { $entry; continue }
                    $changed = $true
                    if ($absolute.StartsWith('\\')) { '\\?\UNC\' + $absolute.Substring(2) }
                    else { '\\?\' + $absolute }
                }
                if ($changed) {
                    [IO.File]::WriteAllLines($list, [string[]]$entries, [Text.UTF8Encoding]::new($false))
                    Write-Host "Extended object paths for CMake exports: $list"
                }
            }
        }
    }
} finally { Pop-Location; $lease.Dispose() }
Remove-Item -LiteralPath $marker
Write-Host "built: $(Join-Path $build "$Title.exe")"
