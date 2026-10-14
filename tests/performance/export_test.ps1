#requires -Version 7.0
$ErrorActionPreference='Stop'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('x360n-export-'+[guid]::NewGuid())
New-Item -ItemType Directory $temp | Out-Null
try {
 $envData=@{title='fixture';label='fixture';seed=@(@{path='profiles/private-profile';bytes=33;sha256='a'});
  scenario=@{description='synthetic';game_root='C:/private-game-root';user_root='C:/private-user-root';start_seconds=5;duration_seconds=5};
  arguments=@('--game_data_root=C:/private-game-root','--user_data_root=C:/private-user-root','--gpu_plugin=xenos');
  hardware=@{cpu='fixture'};artifacts=@(@{file='game.exe';sha256='a'})}
 $envData | ConvertTo-Json -Depth 10 | Set-Content "$temp/environment.json"
 @{scene_verified=$false;gpu_ms_per_frame=$null;fps_mean=60} | ConvertTo-Json | Set-Content "$temp/result.json"
 & "$PSScriptRoot/../../scripts/export-benchmark.ps1" -Run $temp -Output "$temp/public.json"
 $raw=Get-Content "$temp/public.json" -Raw;$r=$raw | ConvertFrom-Json
 if($raw -match 'private-profile|private-game-root|private-user-root'){throw 'Private input escaped export'}
 if($r.seed_inventory_sha256.Length -ne 64 -or $r.result.scene_verified -or $null -ne $r.result.gpu_ms_per_frame -or $r.result.fps_mean -ne 60){throw 'Evidence transformed incorrectly'}
 if($r.evidence.Count -ne 2 -or $r.settings[0] -ne '--gpu_plugin=xenos'){throw 'Evidence/settings missing'}
 'Benchmark export: measurements, unavailable fields, provenance and private path exclusion passed'
} finally {
 Remove-Item -LiteralPath "$temp/environment.json","$temp/result.json","$temp/public.json" -ErrorAction SilentlyContinue
 Remove-Item -LiteralPath $temp
}
