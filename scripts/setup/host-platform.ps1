# Package platform of this machine and the locked tools that belong to it.
# Dot-sourced by preparation and packaging; works in powershell.exe 5.1 and pwsh 7.

# <windows|linux|macos>-<x64|arm64>. Windows reads the machine's native
# architecture, never the one an emulated process sees.
function Get-HostPlatform {
    if ($env:OS -eq 'Windows_NT') {
        $system = 'windows'
        $architecture = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment').PROCESSOR_ARCHITECTURE
    } else {
        $information = [Runtime.InteropServices.RuntimeInformation]
        $system = if ($information::IsOSPlatform([Runtime.InteropServices.OSPlatform]::OSX)) { 'macos' } else { 'linux' }
        $architecture = [string]$information::OSArchitecture
    }
    switch ($architecture) {
        'AMD64' { return "$system-x64" }
        'X64' { return "$system-x64" }
        'ARM64' { return "$system-arm64" }
    }
    throw "Unsupported host architecture: $architecture"
}

# Tools of desktop-tools.lock.json for a Windows platform; entries without one are windows-x64.
function Select-PlatformTools($Lock, [string]$Platform) {
    @($Lock.tools | Where-Object { $(if ($_.PSObject.Properties['platform']) { $_.platform } else { 'windows-x64' }) -eq $Platform })
}

# The locked Windows LLVM of deps.lock.json for a Windows platform.
function Get-LockedWindowsLlvm($Dependencies, [string]$Platform) {
    switch ($Platform) {
        'windows-x64' { return $Dependencies.llvm }
        'windows-arm64' { return $Dependencies.llvm_windows_arm64 }
    }
    throw "No locked Windows LLVM for $Platform"
}
