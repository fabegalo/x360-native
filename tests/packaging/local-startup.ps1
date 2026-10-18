# Exercise selection only; the fake executables below are never launched.
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$scratch=Join-Path $root ('.local/evidence/startup-test-'+[guid]::NewGuid().ToString('N')+' with spaces')
New-Item -ItemType Directory -Force "$scratch/scripts","$scratch/.local/build/desktop-only/src/desktop" | Out-Null
Copy-Item "$root/scripts/start-desktop.cmd" "$scratch/scripts/start-desktop.cmd"
$cache="$scratch/.local/build/desktop-only/CMakeCache.txt"
$local="$scratch/.local/build/desktop-only/src/desktop/x360-native.exe"
[IO.File]::WriteAllText($local,'unexecutable selection fixture')
[IO.File]::WriteAllText($cache,"CMAKE_BUILD_TYPE:STRING=Debug`r`n")
$text=& "$scratch/scripts/start-desktop.cmd" --print-path
if($LASTEXITCODE -ne 2){throw 'Startup accepted Debug'}
[IO.File]::WriteAllText($cache,"CMAKE_BUILD_TYPE:STRING=Release`r`n")
$text=& "$scratch/scripts/start-desktop.cmd" --print-path
if($LASTEXITCODE -or [IO.Path]::GetFullPath($text) -ne [IO.Path]::GetFullPath($local)){throw 'Startup did not select local Release'}
[IO.File]::WriteAllText($cache,"CMAKE_BUILD_TYPE:STRING=RelWithDebInfo`r`n")
$fallback="$scratch/.local/build/win-amd64/src/desktop/Release/x360-native.exe"
New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($fallback)) | Out-Null
[IO.File]::WriteAllText($fallback,'unexecutable selection fixture')
$text=& "$scratch/scripts/start-desktop.cmd" --print-path
if($LASTEXITCODE -or [IO.Path]::GetFullPath($text) -ne [IO.Path]::GetFullPath($fallback)){throw 'Startup did not use Release fallback'}
'PASS: Debug rejected; local Release selected; Release-only fallback selected; spaces handled; no UI/game executed'
