# local.motocross.shared-xam: Motocross Madness with the avatar service and the
# 17559 Guide in one process. Both must use the single original XAM image bound
# by the dashboard's system UI host (no second copy at 0x815F0000). Bounded run
# through `x360n run` (receipt-checked), no desktop input: the opt-in
# dash17559_guide_probe_seconds requests the original Guide service once.
# Needs the prepared title and dashboard builds and the user's originals;
# exits 77 (skipped) otherwise. Opens a window, so it runs alone.
param(
  [string]$Supervisor,
  # Re-checks the rex.log of an earlier run instead of starting the title.
  [string]$ExistingLog = '',
  [ValidateRange(20, 120)][int]$Seconds = 55,
  [ValidateRange(5, 100)][int]$GuideAfter = 30
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$exe = "$root\.local\build\titles\motocross_madness\motocross_madness.exe"
$host_dll = "$root\.local\build\titles\dash_17559\dash_17559_session_ui.dll"
$dash = "$root\.local\inputs\dashboard\17559"
$xam = "$dash\system\xam_ui.xex"
$catalog = "$dash\avatar\AvatarAssetPack.toc"
$game = "$root\.local\inputs\titles\motocross_madness\extracted"
if ($ExistingLog) {
  $log = (Resolve-Path -LiteralPath $ExistingLog).Path
} else {
if (-not $Supervisor) { throw 'Supervisor is required to start the title' }
foreach ($file in @($exe, $host_dll, $xam, $catalog, "$game\default.xex")) {
  if (-not (Test-Path -LiteralPath $file)) { Write-Host "SKIP: not present: $file"; exit 77 }
}
# Never the user's saves: a fresh title user root for this run only.
$user = "$root\.local\tmp\shared-xam-" + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff')
New-Item -ItemType Directory -Path $user | Out-Null
$label = 'mm-shared-xam'
$arguments = @('run', $exe, '--label', $label, '--timeout', "$Seconds", '--rex-log', '--',
  '--gpu_plugin=xenos', '--mnk_mode=true', '--gpu_allow_invalid_fetch_constants=true',
  "--game_data_root=$game", "--user_data_root=$user",
  '--x360n_dashboard_version=2.0.17559.0',
  "--x360n_system_ui_host=$host_dll", "--x360n_system_ui_root=$dash",
  "--x360n_avatar_xam_17559=$xam", "--x360n_avatar_catalog_17559=$catalog",
  "--dash17559_guide_probe_seconds=$GuideAfter")
$output = & $Supervisor --root $root @arguments 2>&1 | ForEach-Object { "$_" }
$output | ForEach-Object { Write-Host $_ }
$summary = $output | Where-Object { $_ -match '^run: .* report (.+report\.json)$' } | Select-Object -Last 1
if (-not $summary) { Write-Host 'FAIL: no supervisor report'; exit 1 }
$report = ($summary -replace '^run: .* report ', '').Trim()
$log = Join-Path (Split-Path $report) 'rex.log'
}
$lines = Get-Content -LiteralPath $log
function First($pattern) {
  for ($i = 0; $i -lt $lines.Count; ++$i) { if ($lines[$i] -match $pattern) { return $i } }
  return -1
}
$failures = @()
$shared = First 'avatar-service: using the session XAM image sys17559:\\system\\xam_ui\.xex'
$ready = First 'avatar-service: READY'
$request = First '17559 system UI: original application request'
$hud = First '17559 system UI: original HUD background -> 00000000'
if ($shared -lt 0) { $failures += 'avatar service did not use the session XAM image' }
if ($ready -lt 0) { $failures += 'avatar service never became READY' }
if ($request -lt 0) { $failures += 'Guide was never requested' }
if ($hud -lt 0) { $failures += 'Guide UI initialization did not complete' }
if ($ready -ge 0 -and $hud -ge 0 -and $hud -lt $ready) { $failures += 'Guide initialized before the avatar service; the shared order was not exercised' }
foreach ($bad in @('Unable to allocate XEX memory', 'XEX load failed', 'already loaded; refusing duplicate load')) {
  if ((First ([regex]::Escape($bad))) -ge 0) { $failures += "log contains '$bad'" }
}
$avatar_errors = @($lines | Where-Object { $_ -match 'avatar-service: (\w+) -> ([0-9A-F]{8})' -and $Matches[2] -ne '00000000' -and
  $Matches[1] -notin @('GetManifestLocalUser', 'SetCustomAsset', 'ManifestGetBodyType') })
if ($avatar_errors) { $failures += "avatar calls failed: $($avatar_errors -join ' | ')" }
Write-Host "log: $log"
Write-Host ("avatar READY line {0}, Guide request line {1}, Guide UI ready line {2}" -f $ready, $request, $hud)
if ($failures) { $failures | ForEach-Object { Write-Host "FAIL: $_" }; exit 1 }
Write-Host 'PASS: one XAM image served the avatar service and the Guide'
exit 0
