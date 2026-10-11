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
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
if ($Title -notmatch '^[a-z][a-z0-9_]*$') { throw "invalid title name '$Title'" }
$titleDir = Join-Path $root "titles\$Title"
if (-not (Test-Path (Join-Path $titleDir 'CMakePresets.json'))) { throw "no title project at $titleDir" }
if (-not $BuildDir) { $BuildDir = ".local/build/titles/$Title" }
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
New-Item -ItemType Directory -Force $build | Out-Null
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
    }
} finally { Pop-Location }
Write-Host "built: $(Join-Path $build "$Title.exe")"
