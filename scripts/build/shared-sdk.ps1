# Immutable generations of the generic SDK. No guest binaries or writable game
# state enter this cache. A bad/interrupted generation is preserved, never reused.
function Assert-SharedSdkSources($Commands, [string]$Sdk, [string]$Build, [Collections.Generic.HashSet[string]]$OwnedSources) {
    $sdkBoundary = [IO.Path]::GetFullPath($Sdk).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $buildBoundary = [IO.Path]::GetFullPath($Build).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    foreach ($entry in @($Commands | ForEach-Object { $_ })) {
        if ($entry.output -notmatch '\.(obj|o|pch|res)$' -or $entry.file -notmatch '\.(c|cc|cpp|cxx|m|mm|rc)$') { continue }
        $file = Resolve-BuildPath $Build $entry.file
        if (-not $file.StartsWith($sdkBoundary,[StringComparison]::OrdinalIgnoreCase) -and
            -not $file.StartsWith($buildBoundary,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Title or external source cannot enter the generic SDK cache: $file"
        }
        if ($file.StartsWith($sdkBoundary,[StringComparison]::OrdinalIgnoreCase) -and
            (-not $OwnedSources -or -not $OwnedSources.Contains($file))) {
            throw "Untracked/unidentified SDK translation unit cannot be shared: $file"
        }
        if ((Test-BuildFile $file) -and ([IO.File]::GetAttributes($file) -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Shared SDK translation unit cannot be a reparse point: $file"
        }
    }
}
function Assert-SharedSdkInputs([string]$Root, $Entries) {
    $boundary = [IO.Path]::GetFullPath($Root).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    foreach ($entry in $Entries) {
        $path = [IO.Path]::GetFullPath($entry.path)
        if (-not $path.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { continue }
        $relative = $path.Substring($boundary.Length).Replace('\','/')
        if ($relative -match '^(titles/|\.local/(aot|inputs|user|keys|preparation)/)') {
            throw "Title-specific or writable content cannot enter the shared SDK: $($entry.path)"
        }
    }
}
function Assert-SharedSdkToolchain([string]$Root, $Ready, [string]$TitleBuild) {
    $original = Join-Path $Ready.build '.preparation/toolchain.json'
    $expected = @($Ready.inputs | Where-Object path -eq $original)
    if ($expected.Count -ne 1) { throw 'Shared SDK compiler inventory is not authenticated' }
    # Query the real producer graph into a separate file. Never rewrite a ready
    # generation. This detects newly shadowing headers in explicit -I paths,
    # including directories that did not exist when the producer was built.
    $probe = Join-Path $Root '.local/cache/shared-sdk/identity/.preparation/verify.json'
    $log = Join-Path $TitleBuild ('x360n-build-sdk-verify.'+[Guid]::NewGuid().ToString('N')+'.log')
    $script = Join-Path $Root 'scripts/build/toolchain-snapshot.ps1'
    if (Invoke-PreparationCommand "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`" -Build `"$($Ready.build)`" -PreviousSha $($expected[0].sha256) -SnapshotOutput `"$probe`"" $log) { throw 'Shared SDK compiler inventory verification failed' }
    if ((Get-ReceiptHash $probe) -ne $expected[0].sha256) { throw 'Shared SDK compiler search inventory changed; rebuild required' }
}
function Read-SharedSdkGeneration([string]$Root, [string]$Pointer, [string]$Identity) {
    $binding = [IO.File]::ReadAllText($Pointer) | ConvertFrom-Json
    $generation = [IO.Path]::GetFullPath($binding.build)
    $boundary = [IO.Path]::GetFullPath((Join-Path $Root '.local/build/shared-sdk')).TrimEnd('\') + '\'
    if (-not $generation.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid shared generation path' }
    $current = $generation
    while ($current.Length -ge $boundary.TrimEnd('\').Length) {
        if (([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Shared SDK generation cannot traverse a reparse point' }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    $receiptFile = Join-Path $generation 'sdk.preparation.json'
    if ((Get-ReceiptHash $receiptFile) -ne $binding.sha256) { throw 'Shared receipt changed' }
    $receipt = [IO.File]::ReadAllText($receiptFile) | ConvertFrom-Json
    if ($receipt.schema -ne 1 -or $receipt.identity -ne $Identity -or
        -not $receipt.inputs -or -not $receipt.artifacts -or -not $receipt.tool -or -not $receipt.producer -or
        -not (Test-ReceiptEntries @($receipt.inputs + $receipt.artifacts)) -or
        -not (Test-ReceiptGlobs $receipt.globs)) { throw 'Shared generation changed' }
    foreach ($file in @($receipt.tool) + @($receipt.producer) + @((Join-Path $generation 'sdk.cmake'), (Join-Path $generation 'targets.cmake'))) {
        if ($file -notin @($receipt.artifacts.path)) { throw 'Unauthenticated shared SDK role' }
    }
    return @{build=$generation; export=(Join-Path $generation 'sdk.cmake'); tool=$receipt.tool;
        producer=$receipt.producer; receipt=$receiptFile; inputs=@($receipt.inputs + $receipt.artifacts); globs=$receipt.globs}
}
function Get-SharedSdkFiles([string]$Directory) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Directory)
    while ($pending.Count) {
        $current = $pending.Pop()
        foreach ($file in [IO.Directory]::EnumerateFiles($current)) {
            if ([IO.Path]::GetFileName($file) -ne '.git') { $file }
        }
        foreach ($child in [IO.Directory]::EnumerateDirectories($current)) {
            if ([IO.Path]::GetFileName($child) -eq '.git' -or
                ($current -eq $Directory -and [IO.Path]::GetFileName($child) -eq 'out')) { continue }
            if (([IO.File]::GetAttributes($child) -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Shared SDK source directory is a reparse point: $child"
            }
            $pending.Push($child)
        }
    }
}
function Get-SharedSdk([string]$Root, [string]$Project, [string]$TitleBuild) {
    Initialize-ProductFingerprints
    $cacheRoot = Join-Path $Root '.local/cache/shared-sdk'
    [void][IO.Directory]::CreateDirectory($cacheRoot)
    # Only one publisher. Readers use immutable generations, with final receipts
    # independently authenticating every source, tool and staged native product.
    $guard = [IO.File]::Open((Join-Path $cacheRoot 'publisher.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        $presetFile = Join-Path $Project 'CMakePresets.json'
        $presets = [IO.File]::ReadAllText($presetFile) | ConvertFrom-Json
        $preset = @($presets.configurePresets | Where-Object name -eq 'x360n-win-amd64')
        if ($preset.Count -ne 1 -or $preset[0].inherits -or $preset[0].generator -ne 'Ninja') {
            throw 'Shared SDK requires an explicit pinned Ninja title preset'
        }
        $variables = [ordered]@{}
        foreach ($property in @($preset[0].cacheVariables.PSObject.Properties | Sort-Object Name)) {
            $value = ([string]$property.Value).Replace('${sourceDir}', $Project.Replace('\','/'))
            if ($value.Contains('${') -or $value.Contains('"') -or $value.Contains("`n")) { throw "Unsupported shared SDK preset value: $($property.Name)" }
            if ($property.Name -in @('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','REXSDK_DIR','PYTHON_EXECUTABLE','CMAKE_TOOLCHAIN_FILE')) {
                $value = [IO.Path]::GetFullPath($value).Replace('\','/')
            }
            $variables[$property.Name] = $value
        }
        if ($variables.CMAKE_BUILD_TYPE -ne 'Release' -or -not $variables.REXSDK_DIR) { throw 'Shared SDK requires pinned Release sources' }
        if ($variables.Contains('X360N_SDK_PATCHES') -and $variables.X360N_SDK_PATCHES -notin @('ON','TRUE','1')) { throw 'Shared SDK requires patch verification' }
        $sdk = [IO.Path]::GetFullPath($variables.REXSDK_DIR)
        if ($sdk -ne [IO.Path]::GetFullPath((Join-Path $Root 'external/rexglue-sdk'))) { throw 'Shared SDK source escaped this environment' }
        $seed = Join-Path $cacheRoot 'identity'
        [void][IO.Directory]::CreateDirectory($seed)
        [IO.File]::WriteAllText((Join-Path $seed 'CMakeCache.txt'), "CMAKE_CXX_COMPILER:FILEPATH=$($variables.CMAKE_CXX_COMPILER)`n")
        $log = Join-Path $TitleBuild ('x360n-build-sdk-identity.' + [Guid]::NewGuid().ToString('N') + '.log')
        $snapshotScript = Join-Path $Root 'scripts/build/toolchain-snapshot.ps1'
        if (Invoke-PreparationCommand "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$snapshotScript`" -Build `"$seed`"" $log) { throw "Shared SDK toolchain inventory failed: $log" }
        $snapshotFile = Join-Path $seed '.preparation/toolchain.json'
        $snapshot = [IO.File]::ReadAllText($snapshotFile) | ConvertFrom-Json
        $files = @(Get-SharedSdkFiles $sdk)
        $files += @(Get-SharedSdkFiles (Join-Path $Root 'cmake'))
        $files += @(Get-SharedSdkFiles (Join-Path $Root 'scripts/build'))
        $files += @(Get-SharedSdkFiles (Join-Path $Root 'patches/rexglue-sdk'))
        $files += @($snapshotFile, (Join-Path $Root 'deps.lock.json'), $PSCommandPath,
            (Join-Path $Root 'scripts/devshell.cmd'), (Join-Path $Root 'scripts/build/shared-sdk.ps1'),
            (Join-Path $Root 'scripts/build/product-fingerprint.cs'))
        foreach ($directory in $snapshot.includes.PSObject.Properties) { $files += @($directory.Value) }
        $files += @($snapshot.libraries)
        foreach ($name in @('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','PYTHON_EXECUTABLE')) {
            if ($variables[$name]) { $files += $variables[$name] }
        }
        foreach ($name in @('cmake.exe','ninja.exe','lld-link.exe','llvm-lib.exe','rc.exe','mt.exe','link.exe','lib.exe')) {
            $resolved = Get-Command $name -ErrorAction SilentlyContinue
            if ($resolved) { $files += $resolved.Source }
        }
        # This function runs outside devshell. Its toolchain inventory and pinned
        # LLVM tree identify the compiler's built-in headers, DLLs and linker too.
        . (Join-Path $Root 'scripts/setup/host-platform.ps1')
        $llvm = Join-Path $Root (Get-LockedWindowsLlvm ([IO.File]::ReadAllText((Join-Path $Root 'deps.lock.json')) | ConvertFrom-Json) (Get-HostPlatform)).install_dir
        $files += @(Get-SharedSdkFiles (Join-Path $llvm 'lib/clang'))
        $files += @([IO.Directory]::EnumerateFiles((Join-Path $llvm 'bin'), '*.dll'))
        foreach ($name in @('clang.exe','clang++.exe','lld-link.exe','llvm-lib.exe')) { $files += Join-Path $llvm "bin/$name" }
        foreach ($tool in @('cmake-3.31.6','ninja-1.13.1','python-3.14.8','mingit-2.56.0.2')) {
            $files += @(Get-SharedSdkFiles (Join-Path $Root ".local/toolchains/$tool"))
        }
        $sdkHead = (& git -C $sdk rev-parse HEAD).Trim()
        $lockedHead = ([IO.File]::ReadAllText((Join-Path $Root 'deps.lock.json')) | ConvertFrom-Json).rexglue_sdk.commit
        if ($LASTEXITCODE -or $sdkHead -ne $lockedHead) { throw 'Shared SDK revision differs from the lock' }
        $inputs = @(Get-ReceiptEntries @($files | Sort-Object -Unique))
        $identity = Get-TextDigest (($variables | ConvertTo-Json -Compress) + ($inputs | ConvertTo-Json -Compress -Depth 4))
        $pointer = Join-Path $cacheRoot "$identity.json"
        if (Test-BuildFile $pointer) {
            try {
                $ready = Read-SharedSdkGeneration $Root $pointer $identity
                Assert-SharedSdkToolchain $Root $ready $TitleBuild
                Write-Host "reused verified shared SDK: $($ready.build)"
                return $ready
            } catch { Write-Host "shared SDK cache rejected: $($_.Exception.Message)" }
        }
        $generation = Join-Path $Root ('.local/build/shared-sdk/' + $identity.Substring(0,16) + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
        [void][IO.Directory]::CreateDirectory($generation)
        $owned = $env:X360N_OWNED_BUILD
        $env:X360N_OWNED_BUILD = $generation
        $lease = Enter-BuildLease $Root $generation ''
        try {
            $arguments = "cmake -S `"$(Join-Path $Root 'cmake/sdk/shared')`" -B `"$generation`" -G Ninja -DX360N_ROOT=`"$Root`""
            foreach ($entry in $variables.GetEnumerator()) { $arguments += " -D$($entry.Key)=`"$($entry.Value)`"" }
            foreach ($step in @(@{name='configure';command=$arguments}, @{name='build';command="cmake --build `"$generation`" --target rexglue rexgpu-xenos"})) {
                Write-Output "X360N_STAGE shared-sdk $($step.name)" | Out-Host
                $log = Join-Path $generation "sdk-$($step.name).log"
                if (Invoke-PreparationCommand $step.command $log) { throw "Shared SDK $($step.name) failed: $log" }
            }
            if (-not (Test-ReleaseBuildConfiguration $generation)) { throw 'Shared SDK is not Release' }
            $commands = @((Invoke-IncrementalNinjaQuery $generation @('compdb','-x')) | ConvertFrom-Json)
            Assert-ReleaseCompileCommands $commands -NinjaGraph
            $ownedSources = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            $tracked = @(& git -c core.quotepath=false -C $sdk ls-files --recurse-submodules)
            if ($LASTEXITCODE) { throw 'Cannot authenticate SDK translation unit ownership' }
            foreach ($file in $tracked) { [void]$ownedSources.Add([IO.Path]::GetFullPath((Join-Path $sdk $file))) }
            $locked = [IO.File]::ReadAllText((Join-Path $Root 'deps.lock.json')) | ConvertFrom-Json
            foreach ($patch in $locked.rexglue_sdk.patches) { foreach ($file in $patch.files.PSObject.Properties.Name) { [void]$ownedSources.Add([IO.Path]::GetFullPath((Join-Path $sdk $file))) } }
            Assert-SharedSdkSources $commands $sdk $generation $ownedSources
            if (-not (Test-ReceiptEntries $inputs)) { throw 'Shared SDK inputs changed while building; no receipt published' }
            $nodes = @(Get-NinjaNodes $generation @('rexglue','rexgpu-xenos'))
            $tool = @($nodes | Where-Object { $_ -match '[\\/]rexglue\.exe$' })[0]
            if (-not $tool) { throw 'Shared SDK codegen producer missing' }
            $producer = @($tool) + @($nodes | Where-Object { $_ -match '\.dll$' -and (Test-BuildFile $_) })
            $exports = @('sdk.cmake','targets.cmake','rexglue-sdk/thirdparty/fmt/fmt-targets.cmake',
                'rexglue-sdk/thirdparty/spdlog/spdlogConfigTargets.cmake','rexglue-sdk/thirdparty/sdl3/SDL3headersTargets.cmake',
                'rexglue-sdk/thirdparty/sdl3/SDL3staticTargets.cmake') | ForEach-Object { Join-Path $generation $_ }
            $artifacts = @(Get-ReceiptEntries @(@($nodes | Where-Object { $_.StartsWith($generation,[StringComparison]::OrdinalIgnoreCase) -and $_ -match '\.(exe|dll|lib)$' -and (Test-BuildFile $_) }) + $exports | Sort-Object -Unique))
            $log = Join-Path $generation 'sdk-toolchain.log'
            if (Invoke-PreparationCommand "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$snapshotScript`" -Build `"$generation`"" $log) { throw 'Shared SDK final toolchain inventory failed' }
            # Keep the actual producer closure in title receipts. The complete
            # source/tool inventory above selects the generation; unrelated LLVM
            # utilities and SDK tests must not tax every subsequent game launch.
            $closure = @(Get-ReceiptEntries @((Get-BuildInputs $Root '' $generation @('rexglue','rexgpu-xenos')) +
                @((Join-Path $Root 'scripts/build/shared-sdk.ps1')) | Sort-Object -Unique))
            Assert-SharedSdkInputs $Root $closure
            $receipt = @{schema=1;identity=$identity;inputs=$closure;artifacts=$artifacts;tool=$tool;producer=$producer;globs=@(Get-BuildGlobs $generation)}
            $receiptFile = Join-Path $generation 'sdk.preparation.json'
            Publish-PreparationReceipt $receiptFile $receipt
            Publish-PreparationReceipt $pointer @{build=$generation;sha256=(Get-ReceiptHash $receiptFile)}
            return @{build=$generation;export=(Join-Path $generation 'sdk.cmake');tool=$tool;producer=$producer;
                receipt=$receiptFile;inputs=@($closure + $artifacts);globs=$receipt.globs}
        } finally {
            foreach ($handle in $lease) { $handle.Dispose() }
            $env:X360N_OWNED_BUILD = $owned
        }
    } finally { $guard.Dispose() }
}
