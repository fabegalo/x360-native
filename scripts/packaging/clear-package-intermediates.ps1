# Successful private packaging jobs retain provenance, sources, logs and products,
# but have no incremental-build contract. Failures/cancellation retain everything.
function Clear-PackageIntermediates([string]$Workspace, [string]$Build) {
    $workspaceRoot = [IO.Path]::GetFullPath($Workspace).TrimEnd('\','/')
    $buildRoot = [IO.Path]::GetFullPath($Build).TrimEnd('\','/')
    $jobsRoot = Join-Path $workspaceRoot '.local/build/distribution'
    if (-not $buildRoot.StartsWith($jobsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        $buildRoot.Substring($jobsRoot.Length+1) -notmatch '^[a-f0-9]{32}[\\/]build$') { throw 'Not a private packaging build' }
    for ($p=$buildRoot; $p; $p=[IO.Path]::GetDirectoryName($p)) {
        if ((Get-Item -Force -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Redirected packaging path: $p" }
    }
    $jobRoot = Split-Path -Parent $buildRoot
    $receipt = Get-Content -Raw -LiteralPath (Join-Path $jobRoot '.x360n-packaging-complete.json') | ConvertFrom-Json
    if ($receipt.schema -ne 1 -or $receipt.status -ne 'complete') { throw 'Packaging success receipt required' }
    $files = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($buildRoot)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -Force -LiteralPath $pending.Pop()) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirected packaging descendant; intermediates retained' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            elseif ($item.Extension -in @('.obj','.o','.pch','.ilk','.idb') -and $item.FullName -match '[\\/]CMakeFiles[\\/]') { $files.Add($item) }
        }
    }
    if (Get-ChildItem -LiteralPath $buildRoot -Recurse -File -Filter '*.preparation.json') { throw 'Prepared products in packaging build; intermediates retained' }
    $journal = Join-Path $jobRoot 'intermediate-cleanup.jsonl'
    $bytes = [long]0
    foreach ($item in $files) {
        # Hash and delete through the same exclusive handle. Failed opens preserve
        # in-use files; no directory deletion and no final executable/DLL removal.
        $stream = [IO.File]::Open($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try {
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
            @{status='intent';path=$item.FullName;sha256=$hash;bytes=$item.Length} | ConvertTo-Json -Compress | Add-Content -LiteralPath $journal
        } finally { $stream.Dispose() }
        # Packaging owns this fresh private tree throughout the job. No other
        # process is given this path, unlike the general maintenance tool.
        Remove-Item -LiteralPath $item.FullName
        @{status='removed';path=$item.FullName} | ConvertTo-Json -Compress | Add-Content -LiteralPath $journal
        $bytes += $item.Length
    }
    Write-Output "Packaging compiler cache removed: $($files.Count) files, $bytes bytes. Sources, products and failure evidence retained."
}
