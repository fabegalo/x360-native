# Distribution dependencies only; no full runtime, title codegen or installation.
param([switch]$RecordEnvironment)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $root
if ($RecordEnvironment) {
    if (-not $env:VCToolsVersion -or -not $env:WindowsSDKVersion) { throw 'Microsoft C++/Windows SDK environment missing' }
    python .github/scripts/release.py environment --versions "$env:VCToolsVersion;$env:WindowsSDKVersion"
    exit $LASTEXITCODE
}
$env:GIT_CONFIG_COUNT = '3'
$env:GIT_CONFIG_KEY_0 = 'core.autocrlf'; $env:GIT_CONFIG_VALUE_0 = 'false'
$env:GIT_CONFIG_KEY_1 = 'core.eol'; $env:GIT_CONFIG_VALUE_1 = 'lf'
$env:GIT_CONFIG_KEY_2 = 'core.longpaths'; $env:GIT_CONFIG_VALUE_2 = 'true'
$lock = Get-Content -Raw deps.lock.json | ConvertFrom-Json
$tree = git ls-tree HEAD external/rexglue-sdk
if ($LASTEXITCODE -ne 0 -or $tree -notmatch '^160000 commit ([0-9a-f]{40})' -or $Matches[1] -ne $lock.rexglue_sdk.commit) {
    throw 'SDK gitlink differs from deps.lock.json'
}
& powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-tools.ps1 -AllowDownloads -ToolsOnly
if ($LASTEXITCODE -ne 0) { throw 'Pinned Git/CMake/Ninja preparation failed' }
$tools = Join-Path $root '.local/toolchains'
$gitDirectory = Join-Path $tools 'mingit-2.56.0.2/cmd'
$env:PATH = $gitDirectory + ';' + $env:PATH
# Each Actions step starts a new shell; retain the locked Git for archive/clone
# calls outside devshell in the existing packager as well.
if ($env:GITHUB_PATH) { [IO.File]::AppendAllText($env:GITHUB_PATH, $gitDirectory + "`n", [Text.UTF8Encoding]::new($false)) }
git submodule update --init --depth 1 external/rexglue-sdk
if ($LASTEXITCODE -ne 0) { throw 'Pinned ReXGlue checkout failed' }
$sdk = Join-Path $root $lock.rexglue_sdk.path
$head = (git -C $sdk rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne $lock.rexglue_sdk.commit) { throw 'SDK checkout differs from the lock' }
$inventory = Get-Content -Raw licenses/inventory.json | ConvertFrom-Json
$paths = @($inventory.desktop_dependencies | ForEach-Object { "thirdparty/$_" })
git -C $sdk submodule update --init --depth 1 --jobs 4 -- @paths
if ($LASTEXITCODE -ne 0) { throw 'Pinned desktop SDK dependencies unavailable' }
foreach ($path in $paths) {
    $entry = git -C $sdk ls-tree HEAD $path
    if ($LASTEXITCODE -ne 0 -or $entry -notmatch '^160000 commit ([0-9a-f]{40})') { throw "Dependency is not pinned: $path" }
    $expected = $Matches[1]
    $actual = (git -C (Join-Path $sdk $path) rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Dependency revision differs: $path" }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File scripts/apply-sdk-patches.ps1
if ($LASTEXITCODE -ne 0) { throw 'Reproducible SDK patch application failed' }
& powershell -NoProfile -ExecutionPolicy Bypass -File scripts/apply-sdk-patches.ps1 -Check
if ($LASTEXITCODE -ne 0) { throw 'SDK patch hash verification failed' }
& powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap-llvm.ps1 -AllowDownloads -Extractor 7zip
if ($LASTEXITCODE -ne 0) { throw 'Pinned LLVM preparation failed' }
# devshell requires the hosted VS C++ workload; fail before building if absent.
& scripts/devshell.cmd cmake --version
if ($LASTEXITCODE -ne 0) { throw 'MSVC/Windows SDK or pinned CMake unavailable on runner' }
& scripts/devshell.cmd ninja --version
if ($LASTEXITCODE -ne 0) { throw 'Pinned Ninja unavailable on runner' }
