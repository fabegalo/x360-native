param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
Add-Type -Path "$Root/scripts/build/product-fingerprint.cs"
$scratch = Join-Path $Root ('.local/build/product-protection-' + [Guid]::NewGuid().ToString('N'))
$directory = Join-Path $scratch 'source with spaces'
[void][IO.Directory]::CreateDirectory($directory)
$file = Join-Path $directory 'input.bin'
[IO.File]::WriteAllText($file, 'first')
$digest = [X360NProducts].GetMethod('Digest', [Reflection.BindingFlags]'Instance,NonPublic')
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
    $entries = [X360NProducts]::ReceiptEntries([string[]]@($file))
    Assert ([X360NProducts]::VerifyReceipt([string[]]@($file), [string[]]@($entries[0].sha256))) 'compiled receipt rejected unchanged file'
    Assert (-not [X360NProducts]::VerifyReceipt([string[]]@("$file-missing"), [string[]]@($entries[0].sha256))) 'compiled receipt accepted missing file'
    $batch = [X360NProducts]::new()
    try {
        $before = $digest.Invoke($batch, [string[]]@($file))
        Assert ($digest.Invoke($batch, [string[]]@($file)) -eq $before) 'protected digest differs'
        $blocked = $false
        try { $writer = [IO.File]::OpenWrite($file); $writer.Dispose() } catch { $blocked = $true }
        Assert $blocked 'writer accepted while digest reusable'
        $blocked = $false
        try { [IO.Directory]::Move($directory, "$directory-moved") } catch { $blocked = $true }
        Assert $blocked 'ancestor rename accepted while path reusable'
    } finally { $batch.Dispose() }
    [IO.Directory]::Move($directory, "$directory-moved")
    $file = Join-Path "$directory-moved" 'input.bin'
    $time = [IO.File]::GetLastWriteTimeUtc($file)
    [IO.File]::WriteAllText($file, 'other'); [IO.File]::SetLastWriteTimeUtc($file, $time)
    Assert (-not [X360NProducts]::VerifyReceipt([string[]]@($file), [string[]]@($entries[0].sha256))) 'compiled receipt cached approval after same-mtime edit'
    $batch = [X360NProducts]::new()
    try { Assert ($digest.Invoke($batch, [string[]]@($file)) -ne $before) 'same-mtime edit reused across transactions' }
    finally { $batch.Dispose() }
    # An outstanding writable mapping must prevent approval, or remain uncached.
    $mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateFromFile($file, 'Open', [NullString]::Value, 0, 'ReadWrite')
    $view = $mapping.CreateViewAccessor()
    $batch = [X360NProducts]::new()
    try {
        $first = $null
        try { $first = $digest.Invoke($batch, [string[]]@($file)) } catch { }
        if ($first) {
            $view.Write(0, [byte]65); $view.Flush()
            Assert ($digest.Invoke($batch, [string[]]@($file)) -ne $first) 'writable mapping approved for reuse'
        }
    } finally { $batch.Dispose(); $view.Dispose(); $mapping.Dispose() }
    # A reparse ancestor may be retargeted even while the old target is held.
    # It must use handle identity again, never the ordinary-path fast path.
    $firstRoot = Join-Path $scratch 'first'; $secondRoot = Join-Path $scratch 'second'
    [void][IO.Directory]::CreateDirectory($firstRoot); [void][IO.Directory]::CreateDirectory($secondRoot)
    [IO.File]::WriteAllText("$firstRoot/input", 'first target')
    [IO.File]::WriteAllText("$secondRoot/input", 'second target')
    $alias = Join-Path $scratch 'alias'
    cmd /c mklink /J "$alias" "$firstRoot" | Out-Null
    if ($LASTEXITCODE) { throw 'Cannot create synthetic junction' }
    $batch = [X360NProducts]::new()
    $blocked = $false
    try {
        $file = Join-Path $alias 'input'
        $before = $digest.Invoke($batch, [string[]]@($file))
        try { [IO.Directory]::Move($alias, "$alias-old") } catch { $blocked = $true }
        if (-not $blocked) {
            cmd /c mklink /J "$alias" "$secondRoot" | Out-Null
            if ($LASTEXITCODE) { throw 'Cannot retarget synthetic junction' }
            Assert ($digest.Invoke($batch, [string[]]@($file)) -ne $before) 'retargeted junction reused old identity'
        }
    } finally { $batch.Dispose() }
    if ($blocked) {
        # Some Windows filesystems also exclude renaming the traversed junction.
        [IO.Directory]::Move($alias, "$alias-old")
        cmd /c mklink /J "$alias" "$secondRoot" | Out-Null
        if ($LASTEXITCODE) { throw 'Cannot retarget released synthetic junction' }
        $batch = [X360NProducts]::new()
        try { Assert ($digest.Invoke($batch, [string[]]@($file)) -ne $before) 'released junction reused old identity' }
        finally { $batch.Dispose() }
    }
    [IO.Directory]::Delete($alias); [IO.Directory]::Delete("$alias-old")
    Write-Output 'PASS: protected hashes, ancestor exclusion, release, fresh transactions and writable mappings'
} catch {
    Write-Output $_.Exception.ToString()
    throw
} finally {
    $absolute = [IO.Path]::GetFullPath($scratch)
    $boundary = [IO.Path]::GetFullPath("$Root/.local/build").TrimEnd('\') + '\'
    if (-not $absolute.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected scratch path' }
    try { [IO.Directory]::Delete($absolute, $true) } catch { Write-Warning "Synthetic scratch retained: $absolute ($($_.Exception.Message))" }
}
