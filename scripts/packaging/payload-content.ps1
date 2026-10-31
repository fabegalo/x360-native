# Only the reviewed launcher font bytes may bypass the font extension exclusion.
# Read policy from the checkout running the gate, never from the package under test.
$launcherFontInventory = @( (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../licenses/inventory.json') -Raw |
    ConvertFrom-Json).components | Where-Object id -eq 'launcher-fonts')
if ($launcherFontInventory.Count -ne 1 -or $launcherFontInventory[0].spdx -ne 'OFL-1.1') { throw 'Missing launcher font provenance' }
$launcherFontHashes = $launcherFontInventory[0].source_files
function Test-LauncherFont([string]$RelativePath, [string]$File) {
    $entry = @($launcherFontHashes.PSObject.Properties | Where-Object { $_.Name -ceq $RelativePath })
    return $entry.Count -eq 1 -and
        (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $entry[0].Value
}
function Assert-SourcePayload([string]$Directory) {
    foreach ($file in Get-ChildItem -LiteralPath $Directory -Recurse -File -Force) {
        $rel = $file.FullName.Substring($Directory.Length + 1).Replace('\','/')
        if ($rel -match '(^|/)(\.local|\.git|external)/' -or
            ($rel -match '\.(xex|xexp|xtt|ttf|otf|pdb|dll|exe|stfs|iso)$' -and -not (Test-LauncherFont $rel $file.FullName)) -or
            ($rel -match '/generated/' -and $rel -notmatch '/generated/rexglue\.cmake$')) {
            throw "Non-distributable payload entry: $rel"
        }
    }
}
