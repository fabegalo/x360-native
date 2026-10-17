# Inventory by default. -Apply migrates only classified outputs and loose scratch
# copies; -Clean additionally removes verified download caches and build residues.
# No originals, user data, worktrees, preparation receipts or final binaries.
#requires -Version 7.0
param([switch]$Apply, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$local = Join-Path $root '.local'
$rules = Get-Content -Raw (Join-Path $PSScriptRoot 'local-layout.json') | ConvertFrom-Json
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fffffff')
$reportDir = Join-Path $local "evidence/maintenance/$stamp"
$actions = [Collections.Generic.List[object]]::new()
$protectedReceiptPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$uncertainReceiptDirectories = [Collections.Generic.List[string]]::new()
$deadline = [DateTime]::UtcNow.AddMinutes(10)
function CheckDeadline { if ([DateTime]::UtcNow -gt $deadline) { throw 'Local maintenance exceeded its ten-minute deadline; inspect the journal before retrying.' } }
function UnderLocal([string]$Relative) {
    $p = [IO.Path]::GetFullPath((Join-Path $local $Relative))
    if (-not $p.StartsWith($local + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Target outside .local: $p" }
    # Check every existing ancestor; never follow a junction into protected data.
    for ($q = $p; $q -ne $local; $q = [IO.Path]::GetDirectoryName($q)) {
        try { $attributes=[IO.File]::GetAttributes($q) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Redirected target: $q" }
    }
    return $p
}
function Record($Entry) {
    $actions.Add($Entry)
    if ($Apply) { $Entry | ConvertTo-Json -Compress -Depth 8 | Add-Content -LiteralPath (Join-Path $reportDir 'journal.jsonl') -Encoding UTF8 }
}
function Inventory([string]$Path) {
    CheckDeadline
    $item = Get-Item -Force -LiteralPath $Path
    $files = if ($item.PSIsContainer) { @(Get-ChildItem -LiteralPath $Path -Force -Recurse -File) } else { @($item) }
    $redirects = if ($item.PSIsContainer) { @(Get-ChildItem -LiteralPath $Path -Force -Recurse -Attributes ReparsePoint) } else { @() }
    foreach ($link in $redirects) {
        if ($link.LinkType -ne 'Junction' -or -not [IO.Path]::IsPathRooted($link.Target) -or
            -not $link.Target.StartsWith($local + '\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Unclassified redirected descendant in $Path : $($link.FullName)"
        }
    }
    $bytes = [long]0
    $lines = @($files | Sort-Object FullName | ForEach-Object {
        CheckDeadline; $bytes += $_.Length
        $_.FullName.Substring($Path.Length) + "`t" + $_.Length + "`t" + $_.LastWriteTimeUtc.Ticks
    })
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $signature = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $links = @($redirects | ForEach-Object { @{ path=$_.FullName.Substring($Path.Length+1); target=$_.Target } })
    return @{ files=$files.Count; bytes=$bytes; metadata_sha256=$signature; links=$links }
}
function Migrate([string]$From, [string]$To) {
    if (@($rules.preserve_in_place) -contains (($From -split '[\\/]')[0])) { throw "Protected local root: $From" }
    $source = UnderLocal $From; $destination = UnderLocal $To
    if (-not (Test-Path -LiteralPath $source)) { return }
    $storageSetting=Join-Path $local 'user-data-root.txt'
    if (Test-Path -LiteralPath $storageSetting -PathType Leaf) {
        $selectedStorage=[IO.Path]::GetFullPath(([IO.File]::ReadAllLines($storageSetting)[0]))
        if ($selectedStorage -eq $source -or $selectedStorage.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Contains selected persistent storage; preserved: $From"
        }
    }
    if (Test-Path -LiteralPath $destination) { throw "Migration destination exists: $destination" }
    if ((Test-Path -LiteralPath $source -PathType Container) -and -not @(Get-ChildItem -LiteralPath $source -Force).Count) {
        Record @{ action='remove-empty-directory'; path=$From; status='planned' }
        if ($Apply) {
            # Nonrecursive deletion also refuses a directory populated meanwhile.
            [IO.Directory]::Delete($source, $false)
            Record @{ action='remove-empty-directory'; path=$From; status='removed' }
        }
        return
    }
    $before = Inventory $source
    Record @{ action='move'; from=$From; to=$To; inventory=$before; status='planned' }
    if ($Apply) {
        New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
        # Same-volume rename; no bytes inside a historical report are rewritten.
        Move-Item -LiteralPath $source -Destination $destination
        $after = Inventory $destination
        if ($before.metadata_sha256 -ne $after.metadata_sha256) { throw "Migration inventory changed: $To" }
        foreach ($link in $before.links) {
            if ($link.target.StartsWith($source + '\',[StringComparison]::OrdinalIgnoreCase)) {
                $linkPath = [IO.Path]::GetFullPath((Join-Path $destination $link.path))
                $targetPath = $destination + $link.target.Substring($source.Length)
                if (-not $linkPath.StartsWith($destination + '\',[StringComparison]::OrdinalIgnoreCase) -or
                    -not (Test-Path -LiteralPath $targetPath -PathType Container) -or
                    (Get-Item -Force -LiteralPath $linkPath).LinkType -ne 'Junction') { throw 'Cannot safely retarget historical junction' }
                Record @{ action='retarget-junction'; path=$linkPath; from=$link.target; to=$targetPath; status='planned' }
                # No recursion: remove only the verified junction entry itself.
                Remove-Item -LiteralPath $linkPath -Force
                New-Item -ItemType Junction -Path $linkPath -Target $targetPath | Out-Null
                Record @{ action='retarget-junction'; path=$linkPath; status='verified' }
            }
        }
        Record @{ action='move'; from=$From; to=$To; inventory=$after; status='verified' }
    }
}
function DeleteFile([string]$Relative, [string]$Reason) {
    CheckDeadline
    $path=UnderLocal $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return }
    if ($protectedReceiptPaths.Contains($path)) {
        Record @{ action='preserve'; path=$Relative; reason='Referenced by an existing preparation receipt' }
        return
    }
    foreach ($directory in $uncertainReceiptDirectories) {
        if ($path.StartsWith($directory+'\',[StringComparison]::OrdinalIgnoreCase)) { return }
    }
    $item=Get-Item -LiteralPath $path
    Record @{ action='delete-file'; path=$Relative; bytes=$item.Length; sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant(); reason=$Reason; status='planned' }
    if ($Apply) { Remove-Item -LiteralPath $path; Record @{ action='delete-file'; path=$Relative; status='removed' } }
}
function CleanHistoricalCMakeCache([string]$Build) {
    $Build=UnderLocal $Build.Substring($local.Length+1)
    $cache=Join-Path $Build 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { return }
    $text=[IO.File]::ReadAllText($cache)
    if ($text -notmatch '^# This is the CMakeCache file\.') { return }
    $cacheHomeMatch=[regex]::Match($text,'(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=([^\r\n]+)')
    if (-not $cacheHomeMatch.Success) { return }
    $source=[IO.Path]::GetFullPath($cacheHomeMatch.Groups[1].Value)
    if ($source -ne $root -and -not $source.StartsWith($root + '\',[StringComparison]::OrdinalIgnoreCase)) { return }
    $generated=UnderLocal ((Join-Path $Build 'CMakeFiles').Substring($local.Length+1))
    if (Test-Path -LiteralPath $generated) {
        $links=@(Get-ChildItem -LiteralPath $generated -Recurse -Force -Attributes ReparsePoint)
        $files=@(Get-ChildItem -LiteralPath $generated -Recurse -Force -File)
        if ($links.Count -or @($files | Where-Object { $_.Extension -in @('.xex','.xexp','.xtt','.iso','.stfs','.sav','.key','.pem','.bundle','.patch') }).Count) {
            Record @{ action='preserve'; path=$generated; reason='Unclassified data or links in a historical CMake cache' }
            return
        }
        foreach ($file in $files) { DeleteFile $file.FullName.Substring($local.Length+1) 'CMake-generated cache/intermediate under a verified historical build; active builds and final artifacts excluded.' }
        if ($Apply -and -not @(Get-ChildItem -LiteralPath $generated -Recurse -Force -File).Count) {
            # Absolute target was checked above; all descendants were enumerated,
            # redirects refused and every file recorded/removed individually.
            Remove-Item -LiteralPath $generated -Recurse -Force
        }
    }
    foreach ($name in @('CMakeCache.txt','build.ninja','build-Debug.ninja','build-Release.ninja','build-RelWithDebInfo.ninja','.ninja_deps','.ninja_log','cmake_install.cmake','compile_commands.json')) {
        DeleteFile ((Join-Path $Build $name).Substring($local.Length+1)) 'Regenerable build-system cache of an inactive historical CMake tree; final binaries and evidence preserved.'
    }
}
if ($Clean -and -not $Apply) { Write-Verbose 'Dry run: nothing will be deleted.' }
if ($Apply) { New-Item -ItemType Directory -Force $reportDir | Out-Null }
$lease=$null
try {
    if ($Apply) { $lease=[IO.File]::Open((Join-Path $local 'environment.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    foreach ($property in $rules.PSObject.Properties) {
        if ($property.Name -eq 'preserve_in_place') { continue }
        foreach ($name in $property.Value) {
            $destination = $property.Name + '/' + $name
            if ($property.Name -eq 'archive/work') { $destination += '-' + $stamp }
            Migrate $name $destination
        }
    }
    # Loose scripts, sources and reports are historical working material, not
    # automatically disposable. Preserve byte hashes and archive them by date.
    foreach ($file in Get-ChildItem -LiteralPath $local -File -Force) {
        if ($file.Extension -in @('.ps1','.py','.cpp','.h','.inc','.s','.patch','.md','.log') -or
            $file.Name -match '^(avatar|ldi_|vavguw-|perf-runtime-|performance-checkpoint)') {
            Record @{ action='scratch-hash'; path=$file.Name; sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() }
            Migrate $file.Name ("archive/scratch/$($stamp.Substring(0,8))/" + $file.Name)
        }
    }
    if ($Clean) {
        # Final outputs, producers and every explicitly certified dependency are
        # authoritative. Compiler objects/PCH may be discarded only when absent
        # from all receipts; opaque/invalid receipts conservatively protect a tree.
        foreach ($receiptFile in Get-ChildItem -LiteralPath (Join-Path $local 'build') -Filter '*.preparation.json' -Recurse -File) {
            try {
                $receiptData=Get-Content -Raw -LiteralPath $receiptFile.FullName | ConvertFrom-Json
                if ($receiptData.schema -ne 1 -or -not $receiptData.inputs -or -not $receiptData.artifacts) {
                    throw 'Unknown preparation receipt schema'
                }
                foreach ($entry in @($receiptData.inputs)+@($receiptData.artifacts)+@($receiptData.producer)) {
                    if ($entry.path) { [void]$protectedReceiptPaths.Add([IO.Path]::GetFullPath($entry.path)) }
                }
                foreach ($glob in $receiptData.globs) {
                    foreach ($file in $glob.files) { [void]$protectedReceiptPaths.Add([IO.Path]::GetFullPath($file)) }
                }
            } catch {
                $uncertainReceiptDirectories.Add($receiptFile.DirectoryName)
                Record @{ action='preserve'; path=$receiptFile.FullName; reason='Unclassified preparation receipt; its build tree is protected' }
            }
        }
        $tools=Get-Content -Raw (Join-Path $root 'scripts/setup/desktop-tools.lock.json') | ConvertFrom-Json
        foreach ($downloadRoot in @('downloads','cache/downloads')) {
            foreach ($tool in $tools.tools) {
                $archive=UnderLocal ($downloadRoot+'/'+$tool.asset)
                $installed=UnderLocal ('toolchains/'+$tool.directory)
                $receipt=Join-Path $installed '.x360n-archive-sha256'
                if ((Test-Path -LiteralPath $archive -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $installed $tool.entry) -PathType Leaf) -and
                    (Test-Path -LiteralPath $receipt) -and (Get-Content -Raw -LiteralPath $receipt).Trim() -eq $tool.sha256 -and
                    (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -eq $tool.sha256) {
                    DeleteFile ($downloadRoot+'/'+$tool.asset) 'Downloaded archive matches pinned lock and installed tool receipt; reproducible from recorded URL.'
                }
            }
            $installer=UnderLocal ($downloadRoot+'/vs_buildtools_2022.exe')
            $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            if ((Test-Path -LiteralPath $installer -PathType Leaf) -and (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
                $installed=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
                if ($LASTEXITCODE -eq 0 -and $installed) {
                    DeleteFile ($downloadRoot+'/vs_buildtools_2022.exe') 'Regenerable Microsoft bootstrapper cache from scripts/setup/install-build-tools.ps1; VS C++ installation independently verified by vswhere.'
                }
            }
        }
        # Compiler intermediates are reproducible; certified inputs, final
        # binaries, receipts, generated AOT and logs remain intact in every tree.
        foreach ($dir in Get-ChildItem -LiteralPath (Join-Path $local 'build') -Directory) {
            if ($dir.Name -eq 'distribution') { continue }
            foreach ($file in Get-ChildItem -LiteralPath $dir.FullName -File -Recurse -Force) {
                if ($file.Extension -in @('.obj','.o','.pch','.ilk','.idb') -and $file.FullName -match '[\\/]CMakeFiles[\\/]') {
                    DeleteFile $file.FullName.Substring($local.Length+1) 'Regenerable CMake compiler intermediate, not referenced by any preparation receipt; certified inputs and final artifacts preserved.'
                }
            }
            if ($dir.Name -notin @('win-amd64','win-amd64-clean','desktop-only','titles','aot')) {
                CleanHistoricalCMakeCache $dir.FullName
            }
        }
        # Private packaging jobs keep source snapshots beside a generated
        # build tree. Preserve sources, indexes, evidence and final artifacts;
        # only verified CMake cache/intermediate directories are disposable.
        $distributionRoots=@((Join-Path $local 'distribution'),(Join-Path $local 'build/distribution'))
        $archivedWork=Join-Path $local 'archive/work'
        if (Test-Path -LiteralPath $archivedWork) {
            $distributionRoots+=@(Get-ChildItem -LiteralPath $archivedWork -Directory -Filter 'distribution-*' | ForEach-Object FullName)
        }
        foreach ($distribution in $distributionRoots) {
            if (-not (Test-Path -LiteralPath $distribution)) { continue }
            foreach ($job in Get-ChildItem -LiteralPath $distribution -Directory) {
                $historicalBuild=Join-Path $job.FullName 'build'
                if (Test-Path -LiteralPath (Join-Path $historicalBuild 'CMakeCache.txt') -PathType Leaf) {
                    if (@(Get-ChildItem -LiteralPath $historicalBuild -Recurse -File -Filter '*.preparation.json').Count) {
                        Record @{ action='preserve'; path=$historicalBuild; reason='Packaging tree contains a preparation receipt; retained conservatively' }
                    } else { CleanHistoricalCMakeCache $historicalBuild }
                }
            }
        }
        # Python bytecode is regenerated from its neighbouring source. Exclude
        # SDK/toolchains/worktrees and arbitrary tmp contents from this sweep.
        foreach ($base in @($PSScriptRoot,(Join-Path $local 'tests'),(Join-Path $local 'tmp'))) {
            if (-not (Test-Path -LiteralPath $base)) { continue }
            foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File -Filter *.pyc) {
                if ($file.FullName.StartsWith($local + '\',[StringComparison]::OrdinalIgnoreCase)) {
                    DeleteFile $file.FullName.Substring($local.Length+1) 'Python bytecode cache; source and working directories preserved.'
                }
            }
        }
    }
    foreach ($relative in @('downloads','cache/downloads','cache')) {
      $cacheDirectory=UnderLocal $relative
      if ($Apply -and (Test-Path -LiteralPath $cacheDirectory) -and -not @(Get-ChildItem -LiteralPath $cacheDirectory -Force).Count) {
        Remove-Item -LiteralPath $cacheDirectory
        Record @{ action='remove-empty-directory'; path=$relative; status='removed' }
      }
    }
} finally {
    if ($lease) { $lease.Dispose() }
    if ($Apply) { $actions | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $reportDir 'summary.json') -Encoding UTF8 }
}
$actions | ConvertTo-Json -Depth 10
