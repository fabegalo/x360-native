$ErrorActionPreference='Stop'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('x360n-compare-'+[guid]::NewGuid())
New-Item -ItemType Directory "$temp/a","$temp/b" | Out-Null
try {
 $envData=@{title='synthetic';scenario=@{start_seconds=1};hardware=@{cpu='fixture'};
  seed=@(@{path='profiles/test';sha256='same'});arguments=@('--gpu_plugin=xenos');
  original_xex=@(@{path='default.xex';sha256='same'});
  collector=@(@{file='benchmark-runtime.ps1';sha256='same'});
  artifacts=@(@{file='game.exe';sha256='same'},@{file='rexruntimerd.dll';sha256='old'})}
 $result=@{window_complete=$true;scene_verified=$true;fps_mean=60;fps_1pct_low=50}
 foreach($side in @('a','b')) {
  $envData | ConvertTo-Json -Depth 10 | Set-Content "$temp/$side/environment.json"
  $result | ConvertTo-Json | Set-Content "$temp/$side/result.json"
 }
 $script=Join-Path $PSScriptRoot '../../scripts/compare-benchmarks.ps1'
 & $script -Before "$temp/a" -After "$temp/b" -Output "$temp/comparison.json" | Out-Null
 if(!(Get-Content "$temp/comparison.json" -Raw | ConvertFrom-Json).comparable){throw 'Identical runs rejected'}
 $envData.artifacts[1].sha256='new'
 $envData | ConvertTo-Json -Depth 10 | Set-Content "$temp/b/environment.json"
 & $script -Before "$temp/a" -After "$temp/b" -Output "$temp/comparison.json" | Out-Null
 if(!(Get-Content "$temp/comparison.json" -Raw | ConvertFrom-Json).comparable){throw 'Allowed runtime change rejected'}
 $envData.arguments=@('--gpu_plugin=other')
 $envData | ConvertTo-Json -Depth 10 | Set-Content "$temp/b/environment.json"
 & $script -Before "$temp/a" -After "$temp/b" -Output "$temp/comparison.json" | Out-Null
 if((Get-Content "$temp/comparison.json" -Raw | ConvertFrom-Json).comparable){throw 'Changed settings accepted'}
 $result.scene_verified=$false;$result | ConvertTo-Json | Set-Content "$temp/b/result.json"
 & $script -Before "$temp/a" -After "$temp/b" -Output "$temp/comparison.json" | Out-Null
 $r=Get-Content "$temp/comparison.json" -Raw | ConvertFrom-Json
 if('Scene captures not reviewed' -notin $r.reasons){throw 'Unreviewed scene accepted'}
 $envData.arguments=@('--gpu_plugin=xenos');$envData.original_xex[0].sha256='different'
 $envData | ConvertTo-Json -Depth 10 | Set-Content "$temp/b/environment.json"
 $result.scene_verified=$true;$result.counters=@{swap_failures=1}
 $result | ConvertTo-Json -Depth 5 | Set-Content "$temp/b/result.json"
 & $script -Before "$temp/a" -After "$temp/b" -Output "$temp/comparison.json" | Out-Null
 $r=Get-Content "$temp/comparison.json" -Raw | ConvertFrom-Json
 if('Different original modules' -notin $r.reasons -or 'Failed swaps require investigation' -notin $r.reasons){throw 'Original mismatch/failed swap accepted'}
 'Comparison: identity, settings, reviewed scenes and explicit runtime allowlist passed'
} finally {
 foreach($side in @('a','b')) {Remove-Item -LiteralPath "$temp/$side/result.json","$temp/$side/environment.json" -ErrorAction SilentlyContinue;Remove-Item -LiteralPath "$temp/$side"}
 Remove-Item -LiteralPath "$temp/comparison.json" -ErrorAction SilentlyContinue
 Remove-Item -LiteralPath $temp
}
