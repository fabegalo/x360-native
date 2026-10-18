$script:X360NProductFingerprintSource = Join-Path $PSScriptRoot 'product-fingerprint.cs'
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
    # A response reference necessarily contains @. Most expanded compdb
    # commands have none; avoid allocating a regex match for every token.
    if ($Command.IndexOf('@') -lt 0) { return }
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
    # Compile once in this PowerShell host. The driver snapshots this source and
    # its SHA-256 participates in both policy and final preparation receipts.
    Initialize-ProductFingerprints
    $graph = Invoke-IncrementalNinjaQuery $Build @('graph','all','rexglue')
    $deps = Invoke-IncrementalNinjaQuery $Build @('deps')
    $commands = @((Invoke-IncrementalNinjaQuery $Build @('compdb','-x')) | ConvertFrom-Json)
    foreach ($command in $commands) { Assert-IncrementalResponseFlags $command.command }
    $libraries = @(); $resources = @()
    $implicit = Join-Path $Build '.preparation/toolchain.json'
    if (Test-BuildFile $implicit) {
        $snapshot = [IO.File]::ReadAllText($implicit) | ConvertFrom-Json
        $libraries = @($snapshot.libraries); $resources = @($snapshot.resources)
    }
    [X360NProducts]::Compute($Build, $graph, $deps, [string[]]@($commands.output),
        [string[]]@($commands.command), [string[]]$libraries, [string[]]$resources)
}
function Initialize-ProductFingerprints {
    if (-not ('X360NProducts' -as [type])) { Add-Type -Path $script:X360NProductFingerprintSource }
}
function Get-IncrementalDatabase([string]$Build) {
    Get-ReceiptEntries @('build.ninja','CMakeFiles/rules.ninja','.ninja_deps','.ninja_log' | ForEach-Object { Join-Path $Build $_ })
}
function Test-FingerprintPolicyTransition($Old, $Current, [string]$Root, $ReceiptInputs) {
    # One explicit, reproducible transition from c84a4b4's signature algorithm.
    # Only its three verifier scripts may differ; SDK, tools, environment,
    # include inventory and every other policy input must remain identical.
    # The caller still authenticates the checkpoint/database, then rehashes all
    # native products and recomputes all signatures before consuming them.
    $known = @{
        'scripts/build-title.ps1'='24dc32959f876804387f934b3b49718db87facd995ec08efe755de6a4e0988dd'
        'scripts/build/incremental-state.ps1'='438e6eecac8bd3199155fd534a57935912bec24c817d83ef37574fe855ee43b9'
        'scripts/build/preparation-receipt.ps1'='03383e4edf99f84a77645ede3a0b3244d770a752a6fa5b7eed861b769d1bf408'
    }
    $oldMap = @{}; $currentMap = @{}; $receiptMap = @{}; $allowed = @{}
    foreach ($entry in $Old) { if ($oldMap.ContainsKey($entry.path)) { return $false }; $oldMap[$entry.path] = $entry.sha256 }
    foreach ($entry in $Current) { if ($currentMap.ContainsKey($entry.path)) { return $false }; $currentMap[$entry.path] = $entry.sha256 }
    foreach ($entry in $ReceiptInputs) { $receiptMap[$entry.path] = $entry.sha256 }
    foreach ($relative in $known.Keys) {
        $path = [IO.Path]::GetFullPath((Join-Path $Root $relative))
        if ($oldMap[$path] -ne $known[$relative] -or $receiptMap[$path] -ne $known[$relative] -or
            -not $currentMap.ContainsKey($path)) { return $false }
        $allowed[$path] = $true
    }
    $helper = [IO.Path]::GetFullPath((Join-Path $Root 'scripts/build/product-fingerprint.cs'))
    if ($oldMap.ContainsKey($helper) -or -not $currentMap.ContainsKey($helper) -or
        $currentMap.Count -ne $oldMap.Count + 1) { return $false }
    foreach ($path in $oldMap.Keys) {
        if (-not $currentMap.ContainsKey($path) -or
            (-not $allowed.ContainsKey($path) -and $oldMap[$path] -ne $currentMap[$path])) { return $false }
    }
    return $true
}
function Read-IncrementalState([string]$File, $Previous, [string]$Build, [string]$Title, [bool]$Interrupted, [string]$Root) {
    try {
        $File = [IO.Path]::GetFullPath($File)
        if ($Interrupted -or -not $Previous -or -not $Previous.incremental -or
            $Previous.incremental.path -ne $File -or -not (Test-ReceiptEntries @($Previous.incremental))) { Write-Verbose 'Checkpoint absent, interrupted or unauthenticated'; return $null }
        $state = [IO.File]::ReadAllText($File) | ConvertFrom-Json
        if ($state.schema -ne 1 -or $state.build -ne $Build -or $state.title -ne $Title -or
            -not $state.products -or -not $state.database -or -not $state.policy -or
            -not (Test-ReceiptEntries $state.database)) { Write-Verbose 'Checkpoint identity or database differs'; return $null }
        if (-not (Test-ReceiptEntries $state.policy)) {
            if (-not $Root -or -not (Test-FingerprintPolicyTransition $state.policy @(Get-IncrementalPolicy $Root $Build) $Root $Previous.inputs)) {
                Write-Verbose 'Checkpoint policy differs'; return $null
            }
            Write-Host 'authenticated fingerprint policy transition; every product will be verified before reuse'
        }
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
        'scripts/build/preparation-receipt.ps1','scripts/build/incremental-state.ps1','scripts/build/product-fingerprint.cs',
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
