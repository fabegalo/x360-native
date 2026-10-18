# Private, disposable acceleration data. The preparation receipt authenticates it.
# Ninja supplies commands/edges; its compiler dep database supplies actual headers.
# No timestamps or cross-title object cache are used as evidence of correctness.
function Invoke-IncrementalNinjaQuery([string]$Build, [string[]]$Query) {
    $cache = Get-BuildCache $Build
    $ninja = $cache.CMAKE_MAKE_PROGRAM
    if ($cache.X360N_REAL_NINJA -and $env:X360N_OWNED_BUILD -eq $Build) { $ninja = $cache.X360N_REAL_NINJA }
    if ([IO.Path]::GetExtension($ninja) -ne '.exe') {
        $output = @(& $ninja -C $Build -t @Query)
        if ($LASTEXITCODE) { throw "Ninja query failed: $($Query -join ' ')" }
        return ($output -join "`n")
    }
    # Read large dep databases as one UTF-8 buffer rather than converting every
    # native output line through two PowerShell pipelines. The caller still owns
    # the same build lease; only read-only queries use the underlying executable.
    $arguments = @('-C', $Build, '-t') + $Query
    $quoted = foreach ($argument in $arguments) {
        '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $ninja; $info.Arguments = $quoted -join ' '
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Ninja query exceeded 60 seconds' }
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode) { throw "Ninja query failed: $errorText" }
        if ($errorText) { Write-Host $errorText.Trim() }
        $stdout.GetAwaiter().GetResult()
    } finally { $process.Dispose() }
}
function Get-TextDigest([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Assert-IncrementalResponseFlags([string]$Command) {
    # compdb -x expands Ninja's rspfile_content, not arbitrary external/nested
    # @files. Their flags/include search paths are absent from our authenticated
    # dependency closure. Fail before certification rather than reuse them.
    if ($Command -match '(?is)^\s*(?:"[^"]*[\\/]cmd\.exe"|[^\s"]*[\\/]cmd\.exe|cmd(?:\.exe)?)\s+(?:/[ds]\s+)*/c\s+"(.*)"\s*$') {
        $Command = $matches[1]
    }
    foreach ($token in [regex]::Matches($Command, '(?:[^\s"'']+|"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*'')+')) {
        $value = $token.Value.Trim([char[]]@([char]34, [char]39))
        if ($value.StartsWith('@') -or
            ($value.StartsWith('-Wl,') -and $value -match ',["'']?@') -or
            $value -match '^-(?:Xclang|Xlinker)=["'']?@') {
            throw 'Unexpanded response file: cannot certify external/nested flags. Declare the response content in the Ninja rule so compdb -x can expand it.'
        }
    }
}
function Get-IncrementalConfigureInputs([string]$Build, [string]$Project) {
    $files = @(Get-NinjaNodes $Build @('build.ninja')) + @(Get-BuildPresetFiles $Project)
    $manifest = Join-Path $Build 'x360n-codegen-manifests.txt'
    if (Test-BuildFile $manifest) { $files += $manifest; $files += [IO.File]::ReadAllLines($manifest) }
    $files += @([IO.Directory]::EnumerateFiles($Project, '*_manifest.toml', [IO.SearchOption]::TopDirectoryOnly))
    # Bootstrap may not yet include a newly emitted sources.cmake in the graph.
    foreach ($directory in @((Join-Path $Project 'generated'), (Join-Path $Build 'generated'))) {
        if ([IO.Directory]::Exists($directory)) { $files += @([IO.Directory]::EnumerateFiles($directory, '*.cmake', [IO.SearchOption]::AllDirectories)) }
    }
    Get-ReceiptEntries @($files | Where-Object { (Test-BuildFile $_) -and $_ -ne (Join-Path $Build 'build.ninja') } | Sort-Object -Unique)
}
function Test-IncrementalConfigureInputs($Current, $Previous) {
    # Compare an explicit configuration inventory, not the receipt's superset
    # of native inputs. Disappearing auxiliary lists/presets matter as much as
    # additions; filtering existing files during discovery must not hide them.
    if (-not $Previous -or @($Current).Count -ne @($Previous).Count) { return $false }
    $hashes = @{}
    foreach ($entry in $Previous) { $hashes[$entry.path] = $entry.sha256 }
    foreach ($entry in $Current) { if ($hashes[$entry.path] -ne $entry.sha256) { return $false } }
    return $true
}
function Get-IncrementalProducts([string]$Build) {
    $cache = Get-BuildCache $Build
    $ninja = $cache.CMAKE_MAKE_PROGRAM
    $graph = (Invoke-IncrementalNinjaQuery $Build @('graph','all','rexglue')) -split '\r?\n'
    $nodes = @{}; $incoming = @{}; $rules = @{}
    foreach ($line in $graph) {
        if ($line -match '^"([^"]+)" \[label="((?:\\.|[^"\\])*)"(, shape=ellipse)?\]$') {
            $id = $matches[1]; $label = $matches[2].Replace('\\', '\').Replace('\"', '"')
            if ($matches[3]) { $rules[$id] = $label }
            else { $nodes[$id] = Resolve-BuildPath $Build $label }
        } elseif ($line -match '^"([^"]+)" -> "([^"]+)"(.*)$') {
            $from = $matches[1]; $to = $matches[2]; $attributes = $matches[3]
            # Order-only dependencies schedule producers; they do not consume
            # their contents (notably codegen and the always-run patch guard).
            if ($attributes -match 'style=dotted') { continue }
            if (-not $incoming.ContainsKey($to)) { $incoming[$to] = [Collections.Generic.List[string]]::new() }
            $incoming[$to].Add($from)
        }
    }
    $dependencies = @{}
    $deps = (Invoke-IncrementalNinjaQuery $Build @('deps')) -split '\r?\n'
    $selected = $null
    foreach ($line in $deps) {
        if ($line -match '^(.+): #deps [0-9]+,.*\((VALID|STALE)\)$') {
            $selected = Resolve-BuildPath $Build $matches[1]
            $dependencies[$selected] = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        } elseif ($selected -and $line.StartsWith('    ')) {
            [void]$dependencies[$selected].Add((Resolve-BuildPath $Build $line.Substring(4)))
        }
    }
    $commandMap = @{}
    foreach ($command in ((Invoke-IncrementalNinjaQuery $Build @('compdb','-x')) | ConvertFrom-Json)) {
        Assert-IncrementalResponseFlags $command.command
        if ($command.command) { $commandMap[(Resolve-BuildPath $Build $command.output)] = $command.command }
    }
    # compdb names just the first output of a multi-output edge. Authenticate
    # and invalidate its siblings as well (embedded headers, import libraries).
    $ruleCommands = @{}
    foreach ($id in $nodes.Keys) {
        if ($commandMap.ContainsKey($nodes[$id])) {
            foreach ($parent in $incoming[$id]) {
                if ($rules.ContainsKey($parent)) { $ruleCommands[$parent] = $commandMap[$nodes[$id]] }
            }
        }
    }
    foreach ($id in $nodes.Keys) {
        foreach ($parent in $incoming[$id]) {
            if ($ruleCommands.ContainsKey($parent)) { $commandMap[$nodes[$id]] = $ruleCommands[$parent] }
        }
    }
    $hashes = @{}
    $implicit = Join-Path $Build '.preparation/toolchain.json'
    $libraryParts = @()
    $resourceParts = @()
    if ([IO.File]::Exists($implicit)) {
        $snapshot = [IO.File]::ReadAllText($implicit) | ConvertFrom-Json
        foreach ($file in @($snapshot.libraries + $snapshot.resources)) {
            if (-not $file) { continue }
            $hashes[$file] = Get-ReceiptHash $file
        }
        $libraryParts = @($snapshot.libraries | ForEach-Object { $_ + '=' + $hashes[$_] })
        $resourceParts = @($snapshot.resources | ForEach-Object { $_ + '=' + $hashes[$_] })
    }
    $emitted = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $boundary = [IO.Path]::GetFullPath($Build).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    foreach ($id in $nodes.Keys) {
        $path = $nodes[$id]
        if (-not $commandMap.ContainsKey($path)) { continue }
        if (-not $emitted.Add($path)) { continue }
        # Only owned products may be removed. Guest originals and source-tree
        # AOT are never disposable native products. SDK handles codegen separately.
        if ($commandMap[$path] -match '(?:^|\s)codegen\s' -or $path -match 'codegen[.]build[.]stamp$') { continue }
        if (-not $path.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw "Build output outside its owner: $path" }
        $inputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($match in [regex]::Matches($commandMap[$path], '/MANIFESTINPUT:(?:"([^"]+)"|([^\s"]+))')) {
            $manifest = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            [void]$inputs.Add((Resolve-BuildPath $Build $manifest))
        }
        foreach ($parent in $incoming[$id]) {
            if ($rules.ContainsKey($parent)) {
                foreach ($inputId in $incoming[$parent]) { if ($nodes.ContainsKey($inputId)) { [void]$inputs.Add($nodes[$inputId]) } }
            } elseif ($nodes.ContainsKey($parent)) { [void]$inputs.Add($nodes[$parent]) }
        }
        if ($dependencies.ContainsKey($path)) { $inputs.UnionWith($dependencies[$path]) }
        # A native object without compiler deps cannot certify its include closure.
        if ($path -match '[.](obj|pch)$' -and (Test-BuildFile $path) -and -not $dependencies.ContainsKey($path)) {
            throw "Compiler dependency record missing: $path"
        }
        $parts = [Collections.Generic.List[string]]::new()
        $parts.Add($commandMap[$path])
        if ($path -match '\.(exe|dll|lib)$') { foreach ($part in $libraryParts) { $parts.Add($part) } }
        if ($path -match '\.res$') { foreach ($part in $resourceParts) { $parts.Add($part) } }
        foreach ($inputPath in @($inputs | Sort-Object)) {
            if (-not $hashes.ContainsKey($inputPath)) {
                $hashes[$inputPath] = if (Test-BuildFile $inputPath) { Get-ReceiptHash $inputPath } else { '<missing>' }
            }
            $parts.Add($inputPath + '=' + $hashes[$inputPath])
        }
        if (-not $hashes.ContainsKey($path)) {
            $hashes[$path] = if (Test-BuildFile $path) { Get-ReceiptHash $path } else { '<missing>' }
        }
        [pscustomobject]@{ path=$path; signature=(Get-TextDigest ($parts -join "`n")); sha256=$hashes[$path] }
    }
}
function Get-IncrementalDatabase([string]$Build) {
    Get-ReceiptEntries @('build.ninja','CMakeFiles/rules.ninja','.ninja_deps','.ninja_log' | ForEach-Object { Join-Path $Build $_ })
}
function Read-IncrementalState([string]$File, $Previous, [string]$Build, [string]$Title, [bool]$Interrupted) {
    try {
        $File = [IO.Path]::GetFullPath($File)
        if ($Interrupted -or -not $Previous -or -not $Previous.incremental -or
            $Previous.incremental.path -ne $File -or -not (Test-ReceiptEntries @($Previous.incremental))) { Write-Verbose 'Checkpoint absent, interrupted or unauthenticated'; return $null }
        $state = [IO.File]::ReadAllText($File) | ConvertFrom-Json
        if ($state.schema -ne 1 -or $state.build -ne $Build -or $state.title -ne $Title -or
            -not $state.products -or -not $state.database -or -not $state.policy -or
            -not (Test-ReceiptEntries $state.database) -or -not (Test-ReceiptEntries $state.policy)) { Write-Verbose 'Checkpoint identity, database or policy differs'; return $null }
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $boundary = $Build.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        foreach ($product in $state.products) {
            if (-not $product.path.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase) -or
                -not $seen.Add($product.path) -or $product.signature -notmatch '^[a-f0-9]{64}$' -or
                $product.sha256 -notmatch '^[a-f0-9]{64}$') { Write-Verbose "Invalid checkpoint product: $($product.path)"; return $null }
        }
        return $state
    } catch { Write-Verbose "Checkpoint rejected: $($_.Exception.Message)"; return $null }
}
function Update-IncrementalProducts([string]$Build, $State, $Processed, [switch]$CheckProducts) {
    $old = @{}
    foreach ($product in $State.products) { $old[$product.path] = $product }
    $removed = 0; $objects = 0
    foreach ($product in @(Get-IncrementalProducts $Build)) {
        $previous = $old[$product.path]
        $key = $product.path + '=' + $product.signature
        $dirty = -not $previous -or $previous.signature -ne $product.signature -or
            ($CheckProducts -and $previous.sha256 -ne $product.sha256)
        if ($dirty -and $Processed.Add($key) -and (Test-BuildFile $product.path)) {
            [IO.File]::Delete((Get-BuildIoPath $product.path))
            $removed++
            if ($product.path -match '[.](obj|pch)$') { $objects++ }
        }
    }
    Write-Host "incremental invalidation: $objects objects, $removed products"
}
function Get-IncrementalPolicy([string]$Root, [string]$Build) {
    $cache = Get-BuildCache $Build
    $files = @('deps.lock.json','scripts/build-title.ps1','scripts/devshell.cmd',
        'scripts/build/preparation-receipt.ps1','scripts/build/incremental-state.ps1',
        'scripts/build/build-lease.ps1','scripts/build/ninja-guard.ps1','scripts/build/toolchain-snapshot.ps1') | ForEach-Object { Join-Path $Root $_ }
    $implicit = Join-Path $Build '.preparation/toolchain.json'
    if ([IO.File]::Exists($implicit)) { $files += $implicit }
    foreach ($name in @('CMAKE_CXX_COMPILER','CMAKE_C_COMPILER','CMAKE_LINKER','CMAKE_AR','X360N_REAL_NINJA','CMAKE_COMMAND','CMAKE_RC_COMPILER','CMAKE_MT','CMAKE_RANLIB')) {
        if ($cache[$name] -and [IO.File]::Exists($cache[$name])) { $files += $cache[$name] }
    }
    $lock = Get-Content -Raw (Join-Path $Root 'deps.lock.json') | ConvertFrom-Json
    $files += @($lock.rexglue_sdk.patches | ForEach-Object { Join-Path $Root $_.file })
    Get-ReceiptEntries @($files | Sort-Object -Unique)
}
