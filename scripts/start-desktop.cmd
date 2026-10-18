@echo off
setlocal
set "desktop_exe=%~dp0..\.local\build\win-amd64\src\desktop\Debug\x360-native.exe"
if not exist "%desktop_exe%" (
  echo Desktop Debug ainda nao compilado. Na raiz do repositorio, execute:
  echo scripts\devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360-native
  exit /b 2
)
start "" /D "%~dp0.." "%desktop_exe%" --root "%~dp0.."
