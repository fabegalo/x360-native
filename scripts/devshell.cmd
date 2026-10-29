@echo off
rem Runs a command inside an MSVC x64 developer environment with the pinned
rem LLVM toolchain first on PATH. Changes apply to this process only.
rem Usage: scripts\devshell.cmd <command> [args...]
setlocal
set "X360_ROOT=%~dp0.."
for %%I in ("%X360_ROOT%") do set "X360_ROOT=%%~fI"
set "X360_LLVM=%X360_ROOT%\.local\toolchains\llvm-22.1.8"

if not exist "%X360_LLVM%\bin\clang++.exe" (
  echo [devshell] LLVM not found at %X360_LLVM%. Run scripts\setup\bootstrap-llvm.ps1 first. 1>&2
  exit /b 3
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [devshell] vswhere.exe not found; install Visual Studio 2022 Build Tools with the C++ workload. 1>&2
  exit /b 3
)
set "VSINSTALL="
for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%P"
if not defined VSINSTALL (
  echo [devshell] No Visual Studio instance with MSVC x64 tools found. 1>&2
  exit /b 3
)

rem Initialize the same native x64 toolset and Windows SDK without optional
rem VS extensions. vcpkg's shell bootstrap can start PowerShell or download tools;
rem this project uses pinned vendored dependencies and does not consume vcpkg.
rem VsDevCmd's core initializes the Windows SDK; its VC extension selects the
rem exact default MSVC toolset used by vcvars64.bat.
if not exist "%VSINSTALL%\Common7\Tools\vsdevcmd\ext\vcvars.bat" (
  echo [devshell] Native Visual Studio environment script missing. 1>&2
  exit /b 3
)
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VSCMD_VCVARSALL_INIT=1"
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -no_ext -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 (
  echo [devshell] Windows SDK environment initialization failed. 1>&2
  exit /b 3
)
call "%VSINSTALL%\Common7\Tools\vsdevcmd\ext\vcvars.bat" >nul
if errorlevel 1 (
  echo [devshell] Native MSVC environment initialization failed. 1>&2
  exit /b 3
)
rem VsDevCmd normally folds the VC extension's include list after all extensions.
rem We called that extension explicitly, so preserve its native include ordering.
set "INCLUDE=%__VSCMD_VCVARS_INCLUDE%%INCLUDE%"
set "EXTERNAL_INCLUDE=%__VSCMD_VCVARS_INCLUDE%%EXTERNAL_INCLUDE%"
set "__VSCMD_VCVARS_INCLUDE="
set "VSCMD_VCVARSALL_INIT="
if not exist "%VCToolsInstallDir%include\vcruntime.h" (
  echo [devshell] Native MSVC headers missing. 1>&2
  exit /b 3
)
set "PATH=%X360_LLVM%\bin;%X360_ROOT%\.local\toolchains\mingit-2.56.0.2\cmd;%X360_ROOT%\.local\toolchains\cmake-3.31.6\bin;%X360_ROOT%\.local\toolchains\ninja-1.13.1;%PATH%"

%*
exit /b %ERRORLEVEL%
