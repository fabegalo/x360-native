# Shared by build-title and its isolated regression tests. No title registry here:
# dependencies come from the configured Ninja graph, compiler deps and SDK depfiles.
function Resolve-BuildPath([string]$Build, [string]$File) {
    if ([IO.Path]::IsPathRooted($File)) { [IO.Path]::GetFullPath($File) }
    else { [IO.Path]::GetFullPath((Join-Path $Build $File)) }
}
function Assert-AotBuildDirectory([string]$Root, [string]$Declared, [string]$Requested) {
    if ($Requested -and (Resolve-BuildPath $Root $Requested) -ne [IO.Path]::GetFullPath($Declared)) {
        throw 'Generated AOT output is bound to its declared build directory; regenerate the project to relocate it.'
    }
}
function Get-BuildIoPath([string]$File) {
    $full = [IO.Path]::GetFullPath($File)
    if ([IO.Path]::DirectorySeparatorChar -eq '\' -and -not $full.StartsWith('\\?\')) {
        if ($full.StartsWith('\\')) { return '\\?\UNC\' + $full.Substring(2) }
        return '\\?\' + $full
    }
    return $full
}
function Test-BuildFile([string]$File) { [IO.File]::Exists((Get-BuildIoPath $File)) }
function Get-ReceiptHash([string]$File) {
    $stream = [IO.File]::OpenRead((Get-BuildIoPath $File))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function Clear-NinjaNativeProducts([string]$Ninja, [string]$Build) {
    $rules = @(& $Ninja -C $Build -t rules | Where-Object { $_ -match '(_COMPILER__|_LINKER__)' })
    if ($LASTEXITCODE -or -not $rules.Count) { throw 'Cannot identify native build rules' }
    # cmd.exe has an 8191-character limit, including the guard/wrapper paths.
    # Bound arguments conservatively; clean every rule, preserving AOT stamps.
    $batch = [Collections.Generic.List[string]]::new()
    $length = 0
    foreach ($rule in $rules) {
        if ($batch.Count -and $length + $rule.Length + 3 -gt 4000) {
            & $Ninja -C $Build -t clean -r @batch
            if ($LASTEXITCODE) { throw 'Cannot invalidate stale native products' }
            $batch.Clear(); $length = 0
        }
        $batch.Add($rule); $length += $rule.Length + 3
    }
    if ($batch.Count) {
        & $Ninja -C $Build -t clean -r @batch
        if ($LASTEXITCODE) { throw 'Cannot invalidate stale native products' }
    }
}
function Reset-GeneratedBuildOutputs([string]$Build, [string[]]$Outputs) {
    $boundary = [IO.Path]::GetFullPath($Build).TrimEnd('\') + '\'
    $aot = [IO.File]::Exists((Join-Path $Build '.x360n-aot-owner'))
    $aotBoundary = [IO.Path]::GetFullPath((Join-Path $Build 'generated')).TrimEnd('\') + '\'
    foreach ($output in $Outputs) {
        $file = Resolve-BuildPath $Build $output
        # Retain title AOT sources/stamps and their per-module fingerprints,
        # including compact generic outputs now located inside the build tree.
        if ($aot -and $file.StartsWith($aotBoundary, [StringComparison]::OrdinalIgnoreCase) -and
            (Test-BuildFile (Join-Path (Split-Path $file) 'codegen.stamp'))) { continue }
        if ($file.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase) -and (Test-BuildFile $file)) {
            [IO.File]::Delete((Get-BuildIoPath $file))
        }
    }
}
function Get-BuildCache([string]$Build) {
    $cache = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Build 'CMakeCache.txt'))) {
        if ($line -match '^([^/#][^:]*):[^=]+=(.*)$') { $cache[$matches[1]] = $matches[2] }
    }
    $cache
}
function Get-NinjaNodes([string]$Build, [string[]]$Targets) {
    $cache = Get-BuildCache $Build
    if (Test-Path Function:\Invoke-IncrementalNinjaQuery) {
        $graph = (Invoke-IncrementalNinjaQuery $Build (@('graph') + $Targets)) -split '\r?\n'
    } else {
        $graph = & $cache.CMAKE_MAKE_PROGRAM -C $Build -t graph @Targets
        if ($LASTEXITCODE) { throw 'Cannot read build dependency graph' }
    }
    foreach ($line in $graph) {
        if ($line -match '^"[^" ]+" \[label="((?:\\.|[^"\\])*)"\]$') {
            $name = $matches[1].Replace('\\', '\').Replace('\"', '"')
            (Resolve-BuildPath $Build $name)
        }
    }
}
function Get-AdditionalCodegenManifests([string]$Project, [string]$Build) {
    $list = Join-Path $Build 'x360n-codegen-manifests.txt'
    if (-not [IO.File]::Exists($list)) { return }
    $boundary = [IO.Path]::GetFullPath($Project).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadAllLines($list)) {
        if (-not $line.Trim()) { continue }
        $file = [IO.Path]::GetFullPath($line)
        if (-not $file.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($file) -notmatch '^[a-z][a-z0-9_]*_manifest\.toml$' -or
            -not [IO.File]::Exists($file)) { throw "Invalid additional codegen manifest: $line" }
        if ($seen.Add($file)) { $file }
    }
}
function Get-BuildInputs([string]$Root, [string]$Title, [string]$Build, [string[]]$Targets, [switch]$AllowInvalidCodegen) {
    $projectDirectory = (Get-BuildCache $Build).CMAKE_HOME_DIRECTORY
    $cache = Get-BuildCache $Build
    $files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $nodes = @(Get-NinjaNodes $Build $Targets)
    $objects = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($node in $nodes) {
        if ($node -match '\.(obj|pch)$') { [void]$objects.Add($node) }
        # Objects, archives and linked shared SDK outputs are rebuilt products.
        # The tool exe/DLL and final artifacts are recorded separately below.
        elseif ($node -notmatch '\.(lib|dll|exe|pdb|stamp)$' -and [IO.File]::Exists($node)) { [void]$files.Add($node) }
        elseif ($node -match '\.lib$' -and -not $node.StartsWith($Build, [StringComparison]::OrdinalIgnoreCase) -and
                -not $node.StartsWith((Join-Path $Root 'external/rexglue-sdk/out'), [StringComparison]::OrdinalIgnoreCase) -and [IO.File]::Exists($node)) { [void]$files.Add($node) }
    }
    if (Test-Path Function:\Invoke-IncrementalNinjaQuery) {
        $deps = (Invoke-IncrementalNinjaQuery $Build @('deps')) -split '\r?\n'
    } else {
        $deps = & $cache.CMAKE_MAKE_PROGRAM -C $Build -t deps
        if ($LASTEXITCODE) { throw 'Cannot read compiler dependencies' }
    }
    $selected = $false
    foreach ($line in $deps) {
        if ($line -match '^(.+): #deps ') {
            $selected = $objects.Contains((Resolve-BuildPath $Build $matches[1]))
        } elseif ($selected -and $line.StartsWith('    ')) {
            [void]$files.Add((Resolve-BuildPath $Build $line.Substring(4)))
        }
    }
    # CMake declares its input files as phony outputs too. Ninja -t inputs
    # follows those empty edges and drops the files. Keep all graph nodes so
    # changing a compile option in an included .cmake invalidates the receipt
    # before configure, even when its mtime is preserved.
    $configs = @(Get-NinjaNodes $Build @('build.ninja'))
    foreach ($file in $configs) {
        $full = (Resolve-BuildPath $Build $file)
        if ([IO.File]::Exists($full)) { [void]$files.Add($full) }
    }
    foreach ($name in @('CMAKE_CXX_COMPILER','CMAKE_C_COMPILER','CMAKE_LINKER','CMAKE_AR','CMAKE_MAKE_PROGRAM','X360N_REAL_NINJA','CMAKE_COMMAND','CMAKE_RC_COMPILER','CMAKE_MT','CMAKE_RANLIB')) {
        if ($cache[$name] -and [IO.File]::Exists($cache[$name])) { [void]$files.Add([IO.Path]::GetFullPath($cache[$name])) }
    }
    foreach ($file in @('deps.lock.json','scripts/build-title.ps1','scripts/build/preparation-receipt.ps1','scripts/devshell.cmd',
                       'scripts/build/build-lease.ps1','scripts/build/ninja-guard.ps1','scripts/build/incremental-state.ps1','scripts/build/toolchain-snapshot.ps1',
                       'cmake/build/x360n_stage_files.cmake','cmake/runtime/x360n_runtime.manifest')) {
        [void]$files.Add([IO.Path]::GetFullPath((Join-Path $Root $file)))
    }
    if ($projectDirectory.StartsWith((Join-Path $Root '.local/aot').Replace('\','/'), [StringComparison]::OrdinalIgnoreCase)) {
        foreach ($name in @('CMakeLists.txt','main.cpp','modules.cmake')) { [void]$files.Add((Join-Path $Root "titles/common/generic/$name")) }
        $moduleAnalysis = Join-Path $Root 'titles/common/module_analysis.toml'
        if ([IO.File]::Exists($moduleAnalysis)) { [void]$files.Add($moduleAnalysis) }
    }
    foreach ($name in @('CMakePresets.json', "${Title}_manifest.toml", 'assets.sha256')) {
        $file = Join-Path $projectDirectory $name
        if ([IO.File]::Exists($file)) { [void]$files.Add($file) }
    }
    $registration = Join-Path $projectDirectory 'preparation.toml'
    if ([IO.File]::Exists($registration)) { [void]$files.Add($registration) }
    $manifestList = Join-Path $Build 'x360n-codegen-manifests.txt'
    if ([IO.File]::Exists($manifestList)) { [void]$files.Add($manifestList) }
    foreach ($file in Get-AdditionalCodegenManifests $projectDirectory $Build) { [void]$files.Add($file) }
    foreach ($file in @('CMakeCache.txt','build.ninja','CMakeFiles/rules.ninja')) { [void]$files.Add((Join-Path $Build $file)) }
    $implicit = Join-Path $Build '.preparation/toolchain.json'
    if ([IO.File]::Exists($implicit)) {
        [void]$files.Add($implicit)
        $snapshot = [IO.File]::ReadAllText($implicit) | ConvertFrom-Json
        foreach ($file in @($snapshot.libraries + $snapshot.resources)) { if ($file) { [void]$files.Add($file) } }
    }
    $lock = Get-Content -Raw (Join-Path $Root 'deps.lock.json') | ConvertFrom-Json
    foreach ($patch in $lock.rexglue_sdk.patches) {
        [void]$files.Add((Join-Path $Root $patch.file))
        foreach ($file in $patch.files.PSObject.Properties.Name) { [void]$files.Add((Join-Path $Root "external/rexglue-sdk/$file")) }
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root 'external/rexglue-sdk/resources/templates') -Recurse -File) { [void]$files.Add($file.FullName) }
    $depfiles = @()
    foreach ($generatedRoot in @((Join-Path $projectDirectory 'generated'), (Join-Path $Build 'generated'))) {
        if (Test-Path -LiteralPath $generatedRoot) {
            $depfiles += @(Get-ChildItem -LiteralPath $generatedRoot -Filter codegen.d -Recurse -File)
        }
    }
    foreach ($depfile in $depfiles) {
        # Old generated modules may remain on disk after removal from a manifest.
        # Only a stamp reachable from this target contributes dependencies.
        if ((Join-Path $depfile.DirectoryName 'codegen.build.stamp') -notin $nodes) { continue }
        # Upstream verifies existence, not the content of emitted outputs.
        # Authenticate its inventory and every output, including ones that do
        # not currently appear in a compiler dependency record.
        [void]$files.Add($depfile.FullName)
        $stampFile = Join-Path $depfile.DirectoryName 'codegen.stamp'
        [void]$files.Add($stampFile)
        try {
            $stamp = [IO.File]::ReadAllText($stampFile) | ConvertFrom-Json
            if ($stamp.version -ne 1 -or -not $stamp.outputs) { throw "Invalid codegen inventory: $stampFile" }
            $boundary = $depfile.DirectoryName.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
            foreach ($output in $stamp.outputs) {
                $full = Resolve-BuildPath $depfile.DirectoryName $output
                if (-not $full.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Codegen inventory escaped its module' }
                [void]$files.Add($full)
            }
        } catch { if (-not $AllowInvalidCodegen) { throw } }
        # SDK writes one dependency per line with make escaping (including spaces).
        foreach ($line in ([IO.File]::ReadAllLines($depfile.FullName) | Select-Object -Skip 1)) {
            $file = $line.Trim() -replace ' \\$', ''
            $file = $file.Replace('\ ', ' ').Replace('\:', ':').Replace('\\', '\').Replace('$$', '$')
            if ($file) { [void]$files.Add([IO.Path]::GetFullPath($file)) }
        }
    }
    @($files | Sort-Object)
}
function Get-ReceiptEntries([string[]]$Files) {
    foreach ($file in $Files) { [pscustomobject]@{ path = [IO.Path]::GetFullPath($file); sha256 = Get-ReceiptHash $file } }
}
function Select-NativeDllOutputs([string]$Root, [string[]]$Nodes) {
    $originals = [IO.Path]::GetFullPath((Join-Path $Root '.local/inputs')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    foreach ($file in $Nodes) {
        # Guest XEX images may be named HostLoader.dll and appear as codegen
        # input nodes. They are immutable inputs, never native staging outputs.
        $full = [IO.Path]::GetFullPath($file)
        if ([IO.Path]::GetExtension($full) -ieq '.dll' -and
            -not $full.StartsWith($originals, [StringComparison]::OrdinalIgnoreCase)) { $full }
    }
}
function Get-PreparedArtifacts([string]$Build, [string]$Exe, [string[]]$DllOutputs) {
    $expected = @($DllOutputs | ForEach-Object { [IO.Path]::GetFileName($_) } | Sort-Object -Unique)
    $actual = @(Get-ChildItem -LiteralPath $Build -Filter '*.dll' -File | ForEach-Object Name | Sort-Object)
    if (($expected -join "`n") -ne ($actual -join "`n")) {
        throw 'DLL inventory differs from the build graph; remove obsolete DLLs from the build directory and prepare again.'
    }
    Get-ReceiptEntries @($Exe)
    foreach ($source in $DllOutputs) {
        $staged = Join-Path $Build ([IO.Path]::GetFileName($source))
        $entry = Get-ReceiptEntries @($staged)
        if ($entry.sha256 -ne (Get-ReceiptHash $source)) { throw "Staged DLL differs from the built output: $staged" }
        $entry
    }
}
function Get-BuildGlobs([string]$Build) {
    $file = Join-Path $Build 'CMakeFiles/VerifyGlobs.cmake'
    if (-not [IO.File]::Exists($file)) { return }
    $glob = $null
    foreach ($line in [IO.File]::ReadAllLines($file)) {
        if ($line -match '^file\(GLOB(_RECURSE)? NEW_GLOB LIST_DIRECTORIES (true|false) "([^"]+)"\)$') {
            $recursive = $matches[1] -eq '_RECURSE'; $directories = $matches[2] -eq 'true'; $full = $matches[3]
            $wildcard = $full.IndexOfAny([char[]]'*?[')
            $separator = $full.LastIndexOf('/', $(if ($wildcard -ge 0) { $wildcard } else { $full.Length - 1 }))
            $directory = [IO.Path]::GetFullPath($full.Substring(0, $separator))
            $pattern = $full.Substring($separator + 1)
            if ($pattern.Contains('[')) { throw 'Unsupported CMake glob in receipt' }
            $glob = @{ directory=$directory; pattern=$pattern; recursive=$recursive; directories=$directories; files=@() }
        } elseif ($glob -and $line -match '^  "([^"]+)"$') { $glob.files += [IO.Path]::GetFullPath($matches[1]) }
        elseif ($glob -and $line -eq '  )') { $glob; $glob = $null }
    }
}
function Test-ReceiptGlobs($Globs) {
    foreach ($glob in $Globs) {
        $expression = '^' + $(if ($glob.recursive) { '([^/]*/)*' } else { '' }) + [regex]::Escape($glob.pattern).Replace('\*', '[^/]*').Replace('\?', '[^/]') + '$'
        $walk = $glob.recursive -or $glob.pattern.Contains('/')
        $actual = @(Get-ChildItem -LiteralPath $glob.directory -Recurse:$walk |
            Where-Object { ($glob.directories -or -not $_.PSIsContainer) -and
                $_.FullName.Substring($glob.directory.Length + 1).Replace('\', '/') -match $expression } | ForEach-Object FullName | Sort-Object)
        $expected = @($glob.files | Sort-Object)
        if (($actual -join "`n") -ne ($expected -join "`n")) { return $false }
    }
    return $true
}
function Test-ReceiptEntries($Entries) {
    foreach ($entry in $Entries) {
        if (-not (Test-BuildFile $entry.path) -or (Get-ReceiptHash $entry.path) -ne $entry.sha256) { return $false }
    }
    return $true
}
function Get-PreviousReceipt([string]$File, [string]$Title, [string]$Manifest, [string]$Exe) {
    if (-not [IO.File]::Exists($File)) { return $null }
    try {
        $r = [IO.File]::ReadAllText($File) | ConvertFrom-Json
        if ($r.schema -ne 1 -or $r.title -ne $Title -or $r.manifest -ne $Manifest -or $r.exe -ne $Exe -or -not $r.inputs -or -not $r.artifacts) { return $null }
        foreach ($required in @($Manifest, $r.roles.compiler, $r.roles.codegen, $r.roles.build_config)) {
            if (-not $required -or [IO.Path]::GetFullPath($required) -notin @($r.inputs.path)) { return $null }
        }
        if ($Exe -notin @($r.artifacts.path)) { return $null }
        $registration = Join-Path (Split-Path $Manifest) 'preparation.toml'
        if ([IO.File]::Exists($registration) -and $registration -notin @($r.inputs.path)) { return $null }
        $artifacts = @($Exe) + @(Get-ChildItem -LiteralPath (Split-Path $Exe) -Filter '*.dll' -File | ForEach-Object FullName)
        if ((@($artifacts | Sort-Object) -join "`n") -ne (@($r.artifacts.path | Sort-Object) -join "`n")) { return $null }
        return $r
    } catch { return $null }
}
function Publish-PreparationReceipt([string]$File, $Receipt) {
    $temp = "$File.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Receipt | ConvertTo-Json -Depth 8))
        $stream = [IO.File]::Open($temp, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        # Same-volume replace/rename publishes one complete document. On failure
        # keep the old receipt and the incomplete marker; never truncate the final.
        if ([IO.File]::Exists($File)) { [IO.File]::Replace($temp, $File, [NullString]::Value) }
        else { [IO.File]::Move($temp, $File) }
    } finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
}
