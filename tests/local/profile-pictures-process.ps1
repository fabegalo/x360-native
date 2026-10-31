param([string]$Checks)
$ErrorActionPreference='Stop'
$workspace=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(-not $Checks) {$Checks=Join-Path $workspace '.local/build/win-amd64/tests/Release/x360n_sdk_runtime_checks.exe'}
$Checks=(Resolve-Path -LiteralPath $Checks).Path
$diagnostics=[IO.Path]::GetFullPath((Join-Path $workspace '.local/diagnostics'))
[IO.Directory]::CreateDirectory($diagnostics) | Out-Null
$roots=@(); $runs=@(); $utf8=New-Object Text.UTF8Encoding($false)
function Assert-Owned([string]$root) {
 $resolved=(Resolve-Path -LiteralPath $root).Path
 if(-not $resolved.StartsWith($diagnostics+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path $resolved -Leaf) -notlike 'picture-process-*' -or
    [IO.File]::ReadAllText((Join-Path $resolved '.picture-process-fixture')) -ne 'x360n-picture-process-v1') {throw 'Unexpected picture fixture'}
 return $resolved
}
function New-PictureFixture {
 $root=Join-Path $diagnostics ('picture-process-'+[guid]::NewGuid().ToString('N'))
 [IO.Directory]::CreateDirectory($root) | Out-Null
 [IO.File]::WriteAllText((Join-Path $root '.picture-process-fixture'),'x360n-picture-process-v1',$utf8)
 $script:roots+= $root
 return (Assert-Owned $root)
}
function Invoke-PicturePhase([string]$root,[string]$phase,[int]$expected=0,[int]$checkpoint=-1) {
 $root=Assert-Owned $root
 $prefix=Join-Path $root ($phase+'-'+[guid]::NewGuid().ToString('N'))
 $oldRoot=$env:X360N_PICTURE_PROCESS_ROOT; $oldPhase=$env:X360N_PICTURE_PROCESS_PHASE
 $oldCheckpoint=$env:X360N_PICTURE_PROCESS_CHECKPOINT
 try {
  $env:X360N_PICTURE_PROCESS_ROOT=$root; $env:X360N_PICTURE_PROCESS_PHASE=$phase
  $env:X360N_PICTURE_PROCESS_CHECKPOINT=[string]$checkpoint
  $process=Start-Process -FilePath $Checks -ArgumentList @('[profile-picture-process-child]','--reporter','compact') -WindowStyle Hidden -PassThru -WorkingDirectory $workspace -RedirectStandardOutput ($prefix+'.stdout.log') -RedirectStandardError ($prefix+'.stderr.log')
  $nativeHandle=$process.Handle
  if(-not $process.WaitForExit(30000)) {$process.Kill();$process.WaitForExit();throw "Picture child timed out: $phase"}
  $code=$process.ExitCode
  if($code -ne $expected) {throw "Picture child $phase exited $code, expected $expected; $prefix.stdout.log"}
  $script:runs+=@{root=$root;phase=$phase;checkpoint=$checkpoint;exit_code=$code}
 } finally {
  $env:X360N_PICTURE_PROCESS_ROOT=$oldRoot; $env:X360N_PICTURE_PROCESS_PHASE=$oldPhase
  $env:X360N_PICTURE_PROCESS_CHECKPOINT=$oldCheckpoint
 }
}
function Package-Path([string]$root) {
 $root=Assert-Owned $root
 $package=[IO.Path]::GetFullPath([IO.File]::ReadAllText((Join-Path $root 'package-path.txt')))
 if(-not $package.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unexpected profile package'}
 return $package
}
foreach($checkpoint in 0..6) {
 $root=New-PictureFixture
 Invoke-PicturePhase $root seed
 Invoke-PicturePhase $root interrupt 93 $checkpoint
 Invoke-PicturePhase $root $(if($checkpoint -eq 6){'new'}else{'old'})
}
$root=New-PictureFixture; Invoke-PicturePhase $root seed
$package=Package-Path $root
$original=[IO.File]::ReadAllBytes((Join-Path $package 'tile_64.png'))
Invoke-PicturePhase $root interrupt 93 0
$ready=Join-Path $package '.x360n-picture-update/ready'
$valid=[IO.File]::ReadAllBytes($ready);$corrupt=$valid.Clone();$corrupt[20]=$corrupt[20] -bxor 128
[IO.File]::WriteAllBytes($ready,$corrupt)
Invoke-PicturePhase $root corrupt
if([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $package 'tile_64.png'))) -ne [Convert]::ToBase64String($original)) {throw 'Corrupt journal changed public bytes'}
[IO.File]::WriteAllBytes($ready,$valid)
Invoke-PicturePhase $root old
$root=New-PictureFixture; Invoke-PicturePhase $root seed
$package=Package-Path $root;$picture=Join-Path $package 'tile_64.png';$original=[IO.File]::ReadAllBytes($picture)
Invoke-PicturePhase $root interrupt 93 0
$foreign=[byte[]](1,3,5,7,9);[IO.File]::WriteAllBytes($picture,$foreign)
Invoke-PicturePhase $root conflict
if([Convert]::ToBase64String([IO.File]::ReadAllBytes($picture)) -ne [Convert]::ToBase64String($foreign)) {throw 'Recovery overwrote a conflicting public file'}
[IO.File]::WriteAllBytes($picture,$original)
Invoke-PicturePhase $root old
$result=@{passed=$true;interruption_checkpoints=7;corrupt_and_conflict_rejected=$true;fixtures=$roots;runs=$runs}
$path=Join-Path $workspace '.local/profile-pictures-process-results.json'
[IO.File]::WriteAllText($path,($result | ConvertTo-Json -Depth 5),$utf8)
Write-Output 'PASS: seven real process interruptions, rollback/commit recovery, corrupt intent and public conflict preservation'
