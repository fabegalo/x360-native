# Corresponding-source gate shared by packaging and its focused synthetic tests.
function Assert-LgplSources($Inventory, [string]$Package, [string]$Payload, [string]$Sdk) {
    $names = @('lzxd.c','lzx.h','system.h','mspack.h','macros.h','readbits.h','readhuff.h')
    $entries = @($Inventory.lgpl_sources)
    if ($entries.Count -ne $names.Count) { throw 'Missing LGPL corresponding source inventory' }
    $seen = @{}
    foreach ($entry in $entries) {
        $name = [IO.Path]::GetFileName([string]$entry.package_path)
        if ($name -notin $names -or $seen.ContainsKey($name) -or
            $entry.package_path -cne "licenses/sources/libmspack/$name" -or
            $entry.sdk_path -cne "thirdparty/libmspack/libmspack/mspack/$name" -or
            $entry.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'Unsafe or conflicting LGPL source inventory' }
        $seen[$name] = $true
        foreach ($base in @($Package,$Payload,$Sdk)) {
            $relative = if ($base -eq $Sdk) { $entry.sdk_path } else { $entry.package_path }
            $file = Join-Path $base $relative
            if (-not [IO.File]::Exists($file)) { throw "Missing required file: $file" }
            if ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked LGPL source refused' }
            $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $entry.sha256) {
                if ($base -eq $Sdk) { throw 'LGPL source provenance differs' }
                throw 'Missing or changed LGPL corresponding source'
            }
        }
        $entry.package_path
    }
}
