param([Parameter(Mandatory)][string]$Log, [double]$Window = 10)
# Summarize cumulative `native_stats` counters into per-window rates and
# per-present averages. Diagnostic only: counters are wall-clock callsite
# costs, overlapping categories, not independent CPU/GPU times.
$ErrorActionPreference = 'Stop'
$rows = New-Object System.Collections.Generic.List[object]
$cur = @{}
foreach ($line in [IO.File]::ReadLines($Log)) {
  if ($line -notmatch '^\[(\S+ \S+)\].*Native (costs|draw costs|frame costs) (.*)$') { continue }
  $t = [datetime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', $null)
  $kind = $Matches[2]
  foreach ($m in [regex]::Matches($Matches[3], '(\w+)=(\d+)')) { $cur[$m.Groups[1].Value] = [double]$m.Groups[2].Value }
  if ($kind -eq 'draw costs') { $rows.Add([pscustomobject]@{ T = $t; D = $cur.Clone() }) }
}
if ($rows.Count -lt 2) { 'no stats rows'; exit 1 }
$t0 = $rows[0].T; $last = $rows[0]
foreach ($r in $rows) {
  $dt = ($r.T - $last.T).TotalSeconds
  if ($dt -lt $Window) { continue }
  $p = $r.D.presents - $last.D.presents
  $parts = @(('{0,5:N0}s' -f ($r.T - $t0).TotalSeconds), ('fps={0:N2}' -f ($p / $dt)))
  foreach ($k in ($r.D.Keys | Sort-Object)) {
    if ($k -eq 'presents' -or -not $last.D.ContainsKey($k)) { continue }
    $v = $r.D[$k] - $last.D[$k]
    if ($k -like '*bytes*') { $v = $v / 1MB; $k = $k -replace 'bytes', 'MB' }
    $parts += if ($p) { '{0}/f={1:N1}' -f $k, ($v / $p) } else { '{0}={1:N0}' -f $k, $v }
  }
  $parts -join ' '
  $last = $r
}
