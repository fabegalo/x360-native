param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference = 'Stop'
$records = foreach ($file in Get-ChildItem -LiteralPath $Directory -Filter 'actions-*.tsv' -File) {
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        $fields = $line.Split("`t")
        if ($fields.Count -ne 4) { throw "Incomplete trace record in $($file.Name)" }
        [pscustomobject]@{
            Process = $file.BaseName; Thread = $fields[1]; Phase = $fields[2]
            TimestampMs = [double]::Parse($fields[0], [Globalization.CultureInfo]::InvariantCulture)
            DurationMs = [double]::Parse($fields[3], [Globalization.CultureInfo]::InvariantCulture)
        }
    }
}
$summary = foreach ($group in $records | Group-Object Phase) {
    $values = @($group.Group.DurationMs | Sort-Object)
    $stats = $values | Measure-Object -Sum -Average -Maximum
    [pscustomobject]@{
        Phase = $group.Name; Count = $values.Count
        TotalMs = [math]::Round($stats.Sum, 3); MeanMs = [math]::Round($stats.Average, 3)
        P95Ms = $values[[math]::Max(0, [math]::Ceiling($values.Count * 0.95) - 1)]
        MaxMs = $stats.Maximum
    }
}
$summary | Sort-Object TotalMs -Descending | Format-Table -AutoSize
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Directory 'summary.json') -Encoding utf8
