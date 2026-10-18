@echo off
setlocal
set "desktop_exe=%~dp0..\.local\build\desktop-only\src\desktop\x360-native.exe"
if not exist "%desktop_exe%" set "desktop_exe=%~dp0..\.local\build\win-amd64\src\desktop\Debug\x360-native.exe"
if not exist "%desktop_exe%" (
  echo Desktop Debug ainda nao compilado. Na raiz do repositorio, execute:
  echo scripts\devshell.cmd cmake -S . -B .local/build/desktop-only -DX360_DESKTOP_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
  echo scripts\devshell.cmd cmake --build .local/build/desktop-only --target x360-native
  exit /b 2
)
start "" /D "%~dp0.." "%desktop_exe%" --root "%~dp0.."
