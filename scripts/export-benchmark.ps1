# Publish measurements and provenance without copying profiles, paths or assets.
#requires -Version 7.0
param([Parameter(Mandatory)][string]$Run,
      [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$environment=Get-Content (Join-Path $Run 'environment.json') -Raw | ConvertFrom-Json
$result=Get-Content (Join-Path $Run 'result.json') -Raw | ConvertFrom-Json
$seedText=(@($environment.seed | ForEach-Object {"$($_.path):$($_.bytes):$($_.sha256)"} | Sort-Object) -join "`n")
$seedHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($seedText))).ToLower()
$evidence=@(Get-ChildItem -LiteralPath $Run -File | Where-Object {$_.Extension -in @('.csv','.json','.png','.log')} | Sort-Object Name | ForEach-Object {
 @{file=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLower()}
})
$public=[ordered]@{
 schema=1;title=$environment.title;label=$environment.label;utc=$environment.utc
 workspace_head=$environment.head;sdk_head=$environment.sdk_head
 workspace_lock_sha256=$environment.lock_sha256
 binary_provenance=$environment.binary_provenance
 hardware=$environment.hardware;artifacts=$environment.artifacts;collector=$environment.collector
 scenario=@{description=$environment.scenario.description;start_seconds=$environment.scenario.start_seconds;duration_seconds=$environment.scenario.duration_seconds;events=$environment.scenario.events}
 settings=@($environment.arguments | Where-Object {$_ -notmatch '^--(game_data_root|log_file|user_data_root|cache_root|perf_log_csv)='})
 seed_inventory_sha256=$seedHash;seed_files=@($environment.seed).Count
 original_xex=$environment.original_xex;cache_condition=$environment.cache_condition
 scene_review=$environment.scene_review;result=$result;evidence=$evidence
 note='Raw evidence stays local; profiles, saves, screenshots, logs and absolute paths are not embedded. Workspace HEAD/lock identify the checkout at launch, not necessarily the frozen binary build.'
}
$public | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Output -Encoding utf8
