#requires -Version 7.0
$ErrorActionPreference='Stop'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('x360n-instrumentation-'+[guid]::NewGuid())
New-Item -ItemType Directory $temp | Out-Null
try {
 $dirs=@();$modes=@('off','on','on','off')
 for($i=0;$i -lt 4;$i++) {
  $dir=Join-Path $temp "$i";New-Item -ItemType Directory $dir | Out-Null;$dirs+=$dir
  $mode=$modes[$i]
  $e=@{title='fixture';label="run-$i";utc=[DateTime]::UtcNow.AddMinutes($i).ToString('o');instrumentation=$mode
   scene_verified=$true;scene_review='fixture';measurement_clock='same';hardware=@{cpu='cpu'};scenario=@{start_seconds=10;duration_seconds=30}
   artifacts=@(@{file='title.exe';sha256='a'},@{file='runtime.dll';sha256='b'});collector=@(@{file='collector';sha256='c'})
   original_xex=@(@{path='default.xex';sha256='d'});seed=@(@{path='seed';sha256='e'})
   arguments=if($mode -eq 'on'){@('--gpu_plugin=xenos','--perf_counters=true','--perf_log_csv=unique.csv')}else{@('--gpu_plugin=xenos','--perf_counters=false')}
  }
  $r=@{instrumentation=$mode;scene_verified=$true;window_complete=$true;external_window_complete=$true
   external_start_seconds=10;external_measured_seconds=30;external_process_cpu_ms_per_second=if($mode -eq 'on'){1100}else{1000}
   ram_working_set_peak_bytes=1234;private_commit_peak_bytes=2345;fps_mean=$null;counters=$null}
  if($mode -eq 'on'){foreach($name in @('fps_mean','fps_1pct_low','frame_ms_mean','frame_ms_p95','frame_ms_p99')){$r[$name]=30};$r.counters=@{swap_failures=0}}
  $e | ConvertTo-Json -Depth 10 | Set-Content "$dir/environment.json"
  $r | ConvertTo-Json -Depth 10 | Set-Content "$dir/result.json"
 }
 function Invoke-FixtureComparison {
  # Use a child so the script's nonzero exit cannot terminate the fixture.
  $command='& "'+(Resolve-Path "$PSScriptRoot/../../scripts/compare-instrumentation.ps1").Path+'" -Runs @('+(@($dirs | ForEach-Object {'"'+$_+'"'}) -join ',')+') -Output "'+"$temp/report.json"+'"'
  & pwsh -NoProfile -Command $command | Out-Null
  return $LASTEXITCODE
 }
 if((Invoke-FixtureComparison) -ne 0){throw 'Valid ABBA rejected'}
 $report=Get-Content "$temp/report.json" -Raw | ConvertFrom-Json
 if([Math]::Abs($report.metrics.external_process_cpu_ms_per_second.change_percent-10) -gt .0001 -or $report.cpu_difference_ci95.low -ne 100){throw 'CPU delta/paired CI failed'}
 if($null -ne $report.metrics.fps_mean.change_percent){throw 'Fabricated OFF FPS comparison'}
 # Nonzero dispersion: adjacent differences 100 and 200 give mean 150,
 # sample SD sqrt(5000), standard error 50 and t(1) margin 635.3102368.
 $varied=Get-Content "$($dirs[2])/result.json" -Raw | ConvertFrom-Json
 $varied.external_process_cpu_ms_per_second=1200
 $varied | ConvertTo-Json -Depth 10 | Set-Content "$($dirs[2])/result.json"
 if((Invoke-FixtureComparison) -ne 0){throw 'Valid varying ABBA rejected'}
 $report=Get-Content "$temp/report.json" -Raw | ConvertFrom-Json
 if([Math]::Abs($report.cpu_difference_ci95.low+485.3102368) -gt .0001 -or [Math]::Abs($report.cpu_difference_ci95.high-785.3102368) -gt .0001){throw 'Nonzero paired uncertainty failed'}
 $validEnv=Get-Content "$($dirs[0])/environment.json" -Raw
 $validResult=Get-Content "$($dirs[0])/result.json" -Raw
 foreach($case in @('scene','binary','enabled','clock','negative','nan','order','failed','window','missing','fabricated','reducer')) {
  $e=$validEnv | ConvertFrom-Json;$r=$validResult | ConvertFrom-Json
  switch($case) {
   'scene' {$e.scene_review=$null}
   'binary' {$e.artifacts[1].sha256='different'}
   'enabled' {$e.arguments+='--perf_log_csv=unexpected.csv'}
   'clock' {$r.external_start_seconds=11}
   'negative' {$r.external_process_cpu_ms_per_second=-1}
   'nan' {$r.external_process_cpu_ms_per_second='NaN'}
   'order' {$e.instrumentation='on'}
   'failed' {$r | Add-Member measurement_error 'bad'}
   'window' {$r.external_window_complete=$false}
   'missing' {$e.collector=$null}
   'fabricated' {$r.fps_mean=60}
   'reducer' {$e | Add-Member reduction @{script_sha256='different';summary_sha256='different';process_summary_sha256='different'}}
  }
  $e | ConvertTo-Json -Depth 10 | Set-Content "$($dirs[0])/environment.json"
  $r | ConvertTo-Json -Depth 10 | Set-Content "$($dirs[0])/result.json"
  if((Invoke-FixtureComparison) -eq 0){throw "Invalid comparison accepted: $case"}
  $rejection=Get-Content "$temp/report.json" -Raw | ConvertFrom-Json
  if($rejection.comparable -or !$rejection.reasons -or $null -ne $rejection.metrics){throw "Invalid comparison did not preserve rejection: $case"}
 }
 'Instrumentation comparison: valid ABBA, zero/nonzero paired uncertainty, unavailable OFF FPS and 12 rejection cases passed'
} finally {
 # Only this fixture's fresh temporary directory is removed.
 if((Split-Path $temp -Leaf) -like 'x360n-instrumentation-*'){Remove-Item -LiteralPath $temp -Recurse -Force}
}
