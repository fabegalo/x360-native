# The policy stores one-way fingerprints, never usable wrapping keys. Scan raw
# bytes and normalized textual byte sequences, including unified-diff prefixes.
if (-not ('X360ExcludedConstants' -as [type])) {
    Add-Type @'
using System;
using System.Security.Cryptography;
public static class X360ExcludedConstants {
    public static bool Contains(byte[] bytes, byte first, string expected) {
        using (var sha = SHA256.Create()) {
            for (int i = 0; i <= bytes.Length - 16; ++i) {
                if (bytes[i] != first) continue;
                var hash = sha.ComputeHash(bytes, i, 16);
                if (BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant() == expected) return true;
            }
        }
        return false;
    }
}
'@
}
function Test-ExcludedConstant([byte[]]$Bytes, $Rules, [switch]$Text) {
    foreach ($rule in $Rules) {
        if ([X360ExcludedConstants]::Contains($Bytes, [byte]$rule.first_byte, $rule.sha256)) { return $true }
    }
    if (-not $Text) { return $false }
    $source = [Text.Encoding]::UTF8.GetString($Bytes)
    # Preserve both removed and added source; a patch ships both. File headers
    # don't contain byte lists. Never strip '+' within a source line.
    $source = [regex]::Replace($source, '(?m)^[ +\-]', '')
    foreach ($match in [regex]::Matches($source, '(?i)0x[0-9a-f]{2}(?:\s*,\s*0x[0-9a-f]{2}){15,}')) {
        $decoded = [byte[]]@([regex]::Matches($match.Value, '(?i)0x([0-9a-f]{2})') | ForEach-Object { [Convert]::ToByte($_.Groups[1].Value, 16) })
        foreach ($rule in $Rules) {
            if ([X360ExcludedConstants]::Contains($decoded, [byte]$rule.first_byte, $rule.sha256)) { return $true }
        }
    }
    foreach ($match in [regex]::Matches($source, '(?i)[0-9a-f]{32,}')) {
        $hex = $match.Value
        $decoded = [byte[]]@(for ($i = 0; $i + 1 -lt $hex.Length; $i += 2) { [Convert]::ToByte($hex.Substring($i, 2), 16) })
        foreach ($rule in $Rules) {
            if ([X360ExcludedConstants]::Contains($decoded, [byte]$rule.first_byte, $rule.sha256)) { return $true }
        }
    }
    return $false
}
