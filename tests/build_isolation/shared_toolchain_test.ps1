param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
. "$Root/scripts/build/preparation-receipt.ps1"
. "$Root/scripts/build/incremental-state.ps1"
. "$Root/scripts/build/shared-sdk.ps1"
$scratch=Join-Path $Root ('.local/build/shared-toolchain-test-'+[Guid]::NewGuid().ToString('N'))
$generation=Join-Path $scratch 'producer';$title=Join-Path $scratch 'consumer'
$headers=Join-Path $scratch 'headers';$future=Join-Path $scratch 'future'
foreach($dir in @($generation,$title,$headers,(Join-Path $generation '.preparation'))){[void][IO.Directory]::CreateDirectory($dir)}
$snapshot=Join-Path $generation '.preparation/toolchain.json'
[IO.File]::WriteAllText($snapshot,'{}') # inventory names are stable, even at first capture
$compiler=Join-Path $Root '.local/toolchains/llvm-22.1.8/bin/clang++.exe'
$ninja=Join-Path $Root '.local/toolchains/ninja-1.13.1/ninja.exe'
[IO.File]::WriteAllText((Join-Path $headers 'value.h'),'#define VALUE 1')
[IO.File]::WriteAllText((Join-Path $generation 'input.cpp'),'#include <value.h>')
[IO.File]::WriteAllText((Join-Path $generation 'CMakeCache.txt'),"CMAKE_CXX_COMPILER:FILEPATH=$compiler`nCMAKE_MAKE_PROGRAM:FILEPATH=$ninja`n")
$command='"'+$compiler+'" -I"'+$headers+'" -I"'+$future+'" -c input.cpp -o output.obj'
[IO.File]::WriteAllText((Join-Path $generation 'build.ninja'),"rule CXX_COMPILER`n  command = $command`nbuild output.obj: CXX_COMPILER input.cpp`nbuild all: phony output.obj`nbuild rexglue: phony`n")
function Invoke-PreparationCommand([string]$Command,[string]$Log){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$env:ComSpec
 $info.Arguments='/d /s /c ""'+$Root+'/scripts/devshell.cmd" '+$Command+' > "'+$Log+'" 2>&1"'
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $process=[Diagnostics.Process]::Start($info)
 try{if(-not $process.WaitForExit(60000)){$process.Kill();throw 'Snapshot fixture timed out'};return $process.ExitCode}finally{$process.Dispose()}
}
$script=Join-Path $Root 'scripts/build/toolchain-snapshot.ps1'
if(Invoke-PreparationCommand "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`" -Build `"$generation`"" (Join-Path $scratch 'initial.log')){throw 'Initial snapshot failed'}
$ready=@{build=$generation;inputs=@(Get-ReceiptEntries @($snapshot))}
Assert-SharedSdkToolchain $Root $ready $title
[void][IO.Directory]::CreateDirectory($future)
[IO.File]::WriteAllText((Join-Path $future 'value.h'),'#define VALUE 2')
$refused=$false
try{Assert-SharedSdkToolchain $Root $ready $title}catch{$refused=$_.Exception.Message -like '*search inventory changed*'}
if(-not $refused){throw 'New explicit include directory was silently reused'}
if(-not(Test-ReceiptEntries $ready.inputs)){throw 'Verification mutated the immutable producer snapshot'}
[IO.Directory]::Move($future,(Join-Path $scratch 'future-preserved'))
Assert-SharedSdkToolchain $Root $ready $title
[IO.File]::WriteAllText((Join-Path $headers 'new-shadow.h'),'new header')
$refused=$false
try{Assert-SharedSdkToolchain $Root $ready $title}catch{$refused=$_.Exception.Message -like '*search inventory changed*'}
if(-not $refused){throw 'New header in an existing search directory was silently reused'}
Write-Output 'PASS: real compiler graph inventories, new/missing include directories, new headers and immutable verification'
