param(
    [Parameter(Mandatory)] [string]$Before,
    [Parameter(Mandatory)] [string]$After,
    [Parameter(Mandatory)] [string]$Output
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Output) { throw 'Output already exists' }
$beforeMeta = Get-Content -Raw (Join-Path $Before 'measurement.json') | ConvertFrom-Json
$afterMeta = Get-Content -Raw (Join-Path $After 'measurement.json') | ConvertFrom-Json
foreach ($key in @('runtime_sha256', 'sdk_commit')) {
    if ($beforeMeta.$key -ne $afterMeta.$key) { throw "Incomparable $key" }
}
function Read-Graph([string]$path, [string]$expectedHash) {
    if ((Get-FileHash $path -Algorithm SHA256).Hash.ToLower() -ne $expectedHash) { throw "Changed graph: $path" }
    $functions = @{}; $emits = @{}; $blocks = @{}; $tables = @(); $errors = @(); $queries = @()
    foreach ($line in [IO.File]::ReadLines((Resolve-Path $path).Path)) {
        $fields = $line.Split("`t")
        switch ($fields[0]) {
            function { $functions[$fields[1]] = $line }
            emit_xxh3 { $emits[$fields[1]] = $fields[2] }
            block { $key = $fields[1]; if (-not $blocks.ContainsKey($key)) { $blocks[$key] = @() }; $blocks[$key] += $line }
            table { $tables += $line }
            error { $errors += $line }
            query { $queries += $line }
        }
    }
    return @{functions=$functions; emits=$emits; blocks=$blocks; tables=$tables; errors=$errors; queries=$queries}
}
$results = @()
foreach ($oldRecord in $beforeMeta.records) {
    $newRecord = @($afterMeta.records | Where-Object { $_.title -eq $oldRecord.title -and $_.variant -eq $oldRecord.variant })
    if ($newRecord.Count -ne 1) { throw 'Corpus variants differ' }
    $newRecord = $newRecord[0]
    foreach ($key in @('input_sha256', 'config_sha256')) {
        if ($oldRecord.$key -ne $newRecord.$key) { throw "Incomparable $key for $($oldRecord.title)" }
    }
    $stem = "$($oldRecord.title)-$($oldRecord.variant)"
    $old = Read-Graph (Join-Path $Before "$stem.tsv") $oldRecord.graph_sha256
    $new = Read-Graph (Join-Path $After "$stem.tsv") $newRecord.graph_sha256
    $added = @($new.functions.Keys | Where-Object { -not $old.functions.ContainsKey($_) } | Sort-Object)
    $removed = @($old.functions.Keys | Where-Object { -not $new.functions.ContainsKey($_) } | Sort-Object)
    $changedEmit = @($old.emits.Keys | Where-Object { $new.emits.ContainsKey($_) -and $old.emits[$_] -ne $new.emits[$_] } | Sort-Object)
    $newlyEmitted = @($new.emits.Keys | Where-Object { -not $old.emits.ContainsKey($_) } | Sort-Object)
    $lostEmitted = @($old.emits.Keys | Where-Object { -not $new.emits.ContainsKey($_) } | Sort-Object)
    $changedBlocks = @($old.blocks.Keys | Where-Object { ($old.blocks[$_] -join "`n") -ne ($new.blocks[$_] -join "`n") } | Sort-Object)
    $tablesEqual = ($old.tables -join "`n") -eq ($new.tables -join "`n")
    $errorsEqual = ($old.errors -join "`n") -eq ($new.errors -join "`n")
    $results += [ordered]@{
        corpus=$stem; before=$oldRecord.summary; after=$newRecord.summary
        added_entries=$added; removed_entries=$removed; changed_existing_emit=$changedEmit
        newly_emitted_entries=$newlyEmitted; lost_emitted_entries=$lostEmitted
        changed_existing_blocks=$changedBlocks; tables_equal=$tablesEqual; errors_equal=$errorsEqual
        before_queries=$old.queries; after_queries=$new.queries
        before_elapsed_ms=$oldRecord.elapsed_ms; after_elapsed_ms=$newRecord.elapsed_ms
        deterministic_graph_equal=($oldRecord.graph_sha256 -eq $newRecord.graph_sha256)
    }
    Write-Output "$stem : added=$($added.Count), removed=$($removed.Count), changed-emit=$($changedEmit.Count), changed-blocks=$($changedBlocks.Count), tables-equal=$tablesEqual"
}
if ($beforeMeta.records.Count -ne $afterMeta.records.Count) { throw 'Corpus variants differ' }
$results | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $Output
