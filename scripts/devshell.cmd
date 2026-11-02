@echo off
rem Runs a command inside a native MSVC developer environment (x64 or ARM64) with
rem the pinned LLVM toolchain first on PATH. Changes apply to this process only.
rem Usage: scripts\devshell.cmd <command> [args...]
setlocal
set "X360_ROOT=%~dp0.."
for %%I in ("%X360_ROOT%") do set "X360_ROOT=%%~fI"
rem The machine's native architecture, also when this shell runs emulated.
set "X360_ARCH="
for /f "tokens=3" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PROCESSOR_ARCHITECTURE ^| findstr /i REG_SZ') do set "X360_ARCH=%%A"
if /i "%X360_ARCH%"=="ARM64" (
  set "X360_LLVM=%X360_ROOT%\.local\toolchains\llvm-22.1.8-windows-arm64"
  set "X360_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"
  set "X360_VS_ARCH=arm64"
) else if /i "%X360_ARCH%"=="AMD64" (
  set "X360_LLVM=%X360_ROOT%\.local\toolchains\llvm-22.1.8"
  set "X360_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
  set "X360_VS_ARCH=amd64"
) else (
  echo [devshell] Unsupported Windows architecture: %X360_ARCH% 1>&2
  exit /b 3
)

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
for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -requires %X360_VC_COMPONENT% -property installationPath`) do set "VSINSTALL=%%P"
if not defined VSINSTALL (
  echo [devshell] No Visual Studio instance with MSVC %X360_VS_ARCH% tools found. 1>&2
  exit /b 3
)

rem Initialize the same native toolset and Windows SDK without optional
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
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -no_ext -arch=%X360_VS_ARCH% -host_arch=%X360_VS_ARCH% >nul
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
rem Below a long installation the SDK's files and its submodules' objects pass
rem MAX_PATH; Git resolves the installation alias to that real path. The
rem environment form also reaches the git processes started for submodules.
set "GIT_CONFIG_COUNT=1"
set "GIT_CONFIG_KEY_0=core.longpaths"
set "GIT_CONFIG_VALUE_0=true"
rem Reached through that alias (x360n ToolPath), MinGit's cmd\git.exe launcher
rem starts git from the real path, where git cannot run its script commands
rem (git submodule); git itself keeps the alias spelling.
set "X360_GIT=%X360_ROOT%\.local\toolchains\mingit-2.56.0.2\cmd"
for %%I in ("%X360_ROOT%") do set "X360_ROOT_ATTRIBUTES=%%~aI"
if /i "%X360_ROOT_ATTRIBUTES:~8,1%"=="l" set "X360_GIT=%X360_ROOT%\.local\toolchains\mingit-2.56.0.2\ucrt64\bin"
set "PATH=%X360_LLVM%\bin;%X360_GIT%;%X360_ROOT%\.local\toolchains\cmake-3.31.6\bin;%X360_ROOT%\.local\toolchains\ninja-1.13.1;%PATH%"

%*
exit /b %ERRORLEVEL%
