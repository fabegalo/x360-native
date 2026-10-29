param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
. "$Root/scripts/build/preparation-receipt.ps1"
$scratch=Join-Path $Root ('.local/build/preparation-reuse-test-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$exe=Join-Path $scratch 'fixture.exe';$manifest=Join-Path $scratch 'fixture_manifest.toml'
$compiler=Join-Path $scratch 'compiler';$tool=Join-Path $scratch 'codegen';$cache=Join-Path $scratch 'CMakeCache.txt'
$dll=Join-Path $scratch 'runtime.dll';$preflight=Join-Path $scratch 'x360n_gpu_preflight.exe'
foreach($file in @($exe,$manifest,$compiler,$tool,$cache,$dll,$preflight)){[IO.File]::WriteAllText($file,'original')}
$receipt=@{schema=1;title='fixture';manifest=$manifest;exe=$exe;inputs=@(Get-ReceiptEntries @($manifest,$compiler,$tool,$cache));
 artifacts=@(Get-ReceiptEntries @($exe,$dll,$preflight));roles=@{compiler=$compiler;codegen=$tool;build_config=$cache}}
$file=$exe+'.preparation.json'
Publish-PreparationReceipt $file $receipt
$ready=Get-PreviousReceipt $file fixture $manifest $exe
if(-not $ready -or -not(Test-ReceiptEntries @($ready.inputs+$ready.artifacts))){throw 'Certified preflight preparation rejected'}
[IO.File]::WriteAllText($preflight,'tampered')
if(Test-ReceiptEntries @($ready.inputs+$ready.artifacts)){throw 'Preflight corruption accepted'}
[IO.File]::Delete($preflight)
if(Get-PreviousReceipt $file fixture $manifest $exe){throw 'Missing preflight accepted'}
$receipt.artifacts=@(Get-ReceiptEntries @($exe,$dll));Publish-PreparationReceipt $file $receipt
if(-not(Get-PreviousReceipt $file fixture $manifest $exe)){throw 'Legacy preparation without preflight rejected'}
$extra=Join-Path $scratch 'foreign.dll';[IO.File]::WriteAllText($extra,'foreign')
if(Get-PreviousReceipt $file fixture $manifest $exe){throw 'Unexpected DLL accepted'}
[IO.File]::Delete($extra)
$receipt.artifacts+=@(Get-ReceiptEntries @($tool));Publish-PreparationReceipt $file $receipt
if(Get-PreviousReceipt $file fixture $manifest $exe){throw 'Arbitrary auxiliary executable/role accepted'}
Write-Output 'PASS: preflight no-op inventory, corruption, removal, foreign DLL and legacy receipts'
