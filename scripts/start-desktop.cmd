@echo off
setlocal
set "desktop_exe=%~dp0..\.local\build\desktop-only\src\desktop\x360-native.exe"
findstr /x /c:"CMAKE_BUILD_TYPE:STRING=Release" "%~dp0..\.local\build\desktop-only\CMakeCache.txt" >nul 2>nul
if errorlevel 1 set "desktop_exe=%~dp0..\.local\build\win-amd64\src\desktop\Release\x360-native.exe"
if not exist "%desktop_exe%" set "desktop_exe=%~dp0..\.local\build\win-amd64\src\desktop\Release\x360-native.exe"
if not exist "%desktop_exe%" (
  echo Desktop Release ainda nao compilado. Na raiz do repositorio, execute:
  echo scripts\devshell.cmd cmake -S . -B .local/build/desktop-only -DX360_DESKTOP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
  echo scripts\devshell.cmd cmake --build .local/build/desktop-only --target x360-native
  exit /b 2
)
if /i "%~1"=="--print-path" (
  for %%I in ("%desktop_exe%") do echo %%~fI
  exit /b 0
)
start "" /D "%~dp0.." "%desktop_exe%" --root "%~dp0.."
