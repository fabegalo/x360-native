param([Parameter(Mandatory)][string]$Build, [string]$PreviousSha, [string]$SnapshotOutput)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
. "$PSScriptRoot/preparation-receipt.ps1"
. "$PSScriptRoot/incremental-state.ps1"
foreach ($name in @('CL','_CL_','LINK','_LINK_','CFLAGS','CXXFLAGS','LDFLAGS','RCFLAGS')) {
    Assert-IncrementalResponseFlags ([Environment]::GetEnvironmentVariable($name))
}
# Run inside devshell: implicit INCLUDE/LIB search paths are not written into
# Ninja's commands. A VS/Windows Kit update must not silently reuse old objects.
$environmentNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in @('PATH','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCToolsVersion',
    'WindowsSdkDir','WindowsSDKVersion','UCRTVersion','CL','_CL_','LINK','_LINK_',
    'CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','COMPILER_PATH',
    'CCC_OVERRIDE_OPTIONS','SOURCE_DATE_EPOCH','CMAKE_PREFIX_PATH','CMAKE_TOOLCHAIN_FILE')) {
    [void]$environmentNames.Add($name)
}
$includes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$resources = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$hasResources = $false
foreach ($directory in @(($env:INCLUDE + ';' + $env:CPATH + ';' + $env:C_INCLUDE_PATH + ';' + $env:CPLUS_INCLUDE_PATH) -split ';')) {
    if ($directory -and [IO.Directory]::Exists($directory)) { [void]$includes.Add([IO.Path]::GetFullPath($directory)) }
}
$cacheFile = Join-Path $Build 'CMakeCache.txt'
$graphTrusted = $false
$snapshotFile = Join-Path $Build '.preparation/toolchain.json'
$dependencyFile = Join-Path $Build '.ninja_deps'
$dependencyHash = if (Test-BuildFile $dependencyFile) { Get-ReceiptHash $dependencyFile } else { $null }
$cachedDirectories = $null
if ($PreviousSha -match '^[a-f0-9]{64}$' -and (Test-BuildFile $snapshotFile) -and
    (Get-ReceiptHash $snapshotFile) -eq $PreviousSha -and $dependencyHash) {
    try {
        $cached = [IO.File]::ReadAllText($snapshotFile) | ConvertFrom-Json
        if ($cached.schema -eq 1 -and $cached.dependency_database -eq $dependencyHash -and $cached.dependency_directories) {
            $cachedDirectories = @($cached.dependency_directories)
        }
    } catch { $cachedDirectories = $null }
}
$dependencyDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if ([IO.File]::Exists($cacheFile)) {
    $cache = Get-BuildCache $Build
    if ($cache.CMAKE_CXX_COMPILER) {
        $clangRoot = Join-Path (Split-Path (Split-Path $cache.CMAKE_CXX_COMPILER)) 'lib/clang'
        if ([IO.Directory]::Exists($clangRoot)) {
            foreach ($version in [IO.Directory]::EnumerateDirectories($clangRoot)) {
                $builtin = Join-Path $version 'include'
                if ([IO.Directory]::Exists($builtin)) { [void]$includes.Add([IO.Path]::GetFullPath($builtin)) }
            }
        }
    }
    $commands = @()
    try {
        $commands = (Invoke-IncrementalNinjaQuery $Build @('compdb','-x')) | ConvertFrom-Json
        $graphTrusted = $true
    } catch { Write-Host 'Toolchain inventory will be completed after configure.' }
    foreach ($command in $commands) { Assert-IncrementalResponseFlags $command.command }
    if ($cache.CMAKE_HOME_DIRECTORY) {
        $configs = @(Get-NinjaNodes $Build @('build.ninja')) + @(Get-BuildPresetFiles $cache.CMAKE_HOME_DIRECTORY)
        foreach ($config in $configs) {
            if ([IO.Path]::GetExtension($config) -notmatch '^\.(cmake|txt|json)$' -or -not (Test-BuildFile $config)) { continue }
            $text = [IO.File]::ReadAllText((Get-BuildIoPath $config))
            foreach ($match in [regex]::Matches($text, '(?i)\$(?:ENV|PENV)\{([a-z_][a-z0-9_]*)\}')) {
                [void]$environmentNames.Add($match.Groups[1].Value)
            }
        }
    }
    # Quoted includes also search beside their source/header, even without -I.
    $localInputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($command in $commands) {
        if ($command.command -and $command.output -match '\.(obj|pch|res)$' -and $command.file) { [void]$localInputs.Add((Resolve-BuildPath $Build $command.file)) }
    }
    if ($graphTrusted -and $cachedDirectories) {
        foreach ($directory in $cachedDirectories) { [void]$dependencyDirectories.Add($directory) }
        Write-Host 'reused authenticated compiler include directories'
    } elseif ($graphTrusted) {
        foreach ($line in ((Invoke-IncrementalNinjaQuery $Build @('deps')) -split '\r?\n')) {
            if ($line.StartsWith('    ')) { [void]$localInputs.Add((Resolve-BuildPath $Build $line.Substring(4))) }
        }
    }
    foreach ($file in $localInputs) {
        $directory = Split-Path $file
        if ((Test-BuildFile $file) -and [IO.Directory]::Exists($directory)) { [void]$dependencyDirectories.Add($directory) }
    }
    foreach ($directory in $dependencyDirectories) { if ([IO.Directory]::Exists($directory)) { [void]$includes.Add($directory) } }
    foreach ($command in $commands) {
        if ($command.output -match '\.res$') {
            $hasResources = $true
            $resourceRoot = Split-Path (Resolve-BuildPath $Build $command.file)
            foreach ($file in [IO.Directory]::EnumerateFiles($resourceRoot, '*', [IO.SearchOption]::AllDirectories)) {
                if ([IO.Path]::GetExtension($file) -match '^\.(h|rc|ico|cur|bmp|manifest)$') { [void]$resources.Add($file) }
            }
        }
        foreach ($match in [regex]::Matches($command.command, '(?:^|\s)(?:-isystem|-iquote|-idirafter|-imsvc|-I|/external:I|/I)\s*(?:"([^"]+)"|([^\s"]+))')) {
            $directory = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            $directory = Resolve-BuildPath $Build $directory
            if ([IO.Directory]::Exists($directory)) { [void]$includes.Add($directory) }
        }
    }
}
$inventory = [ordered]@{}
$inventoryRoots = [Collections.Generic.List[string]]::new()
foreach ($directory in @($includes | Sort-Object)) {
    $covered = $false
    foreach ($parent in $inventoryRoots) {
        if ($directory.StartsWith($parent.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { $covered = $true; break }
    }
    if ($covered) { continue }
    $inventoryRoots.Add($directory)
    # Contents of consumed headers are hashed through compiler deps. Inventory
    # changes also detect a newly added header shadowing a previous resolution.
    $headers = [Collections.Generic.List[string]]::new()
    foreach ($file in [IO.Directory]::EnumerateFiles($directory, '*', [IO.SearchOption]::AllDirectories)) {
        $headers.Add($file)
    }
    $sorted = $headers.ToArray()
    [Array]::Sort($sorted, [StringComparer]::CurrentCultureIgnoreCase)
    $inventory[$directory] = $sorted
    if ($hasResources) { foreach ($file in $inventory[$directory]) { if ([IO.Path]::GetExtension($file) -match '^\.(h|hh|hpp|hxx|inc|inl|rc|ico|cur|bmp|manifest)$') { [void]$resources.Add($file) } } }
}
$libraries = @()
foreach ($directory in @($env:LIB -split ';')) {
    if ($directory -and [IO.Directory]::Exists($directory)) {
        $libraries += @([IO.Directory]::EnumerateFiles($directory, '*.lib', [IO.SearchOption]::TopDirectoryOnly))
    }
}
$variables = [ordered]@{}
foreach ($name in @($environmentNames | Sort-Object)) {
    $value = [Environment]::GetEnvironmentVariable($name)
    # Configs may reference credentials as well as flags; persist identity only.
    $variables[$name] = if ($null -eq $value) { $null } else { Get-TextDigest $value }
}
$snapshot = [ordered]@{schema=1; graph_trusted=$graphTrusted; environment=$variables; includes=$inventory;
    dependency_database=$dependencyHash; dependency_directories=@($dependencyDirectories | Sort-Object);
    libraries=@($libraries | Sort-Object -Unique); resources=@($resources | Sort-Object)}
$file = if ($SnapshotOutput) { [IO.Path]::GetFullPath($SnapshotOutput) } else { Join-Path $Build '.preparation/toolchain.json' }
[void][IO.Directory]::CreateDirectory((Split-Path $file))
$text = $snapshot | ConvertTo-Json -Depth 5
if (-not [IO.File]::Exists($file) -or [IO.File]::ReadAllText($file) -ne $text) {
    Publish-PreparationReceipt $file $snapshot
}
