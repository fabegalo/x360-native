# Compatibility entry point retained by the pinned lock's patches_note and
# existing local setup scripts. Implementation belongs to setup/.
& (Join-Path $PSScriptRoot 'setup/apply-sdk-patches.ps1') @args
exit $LASTEXITCODE
