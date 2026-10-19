# Copyright (c) 2026, x360-native. SPDX-License-Identifier: BSD-3-Clause
# Application version computed from the Conventional Commits of the whole history,
# starting at 0.0.0 and replaying every commit reachable from -Revision, oldest first:
#   feat            -> minor (patch resets)
#   fix, perf       -> patch
#   breaking change -> major; while the major is 0, minor (1.0 is a deliberate decision)
#   "Release-As: X.Y.Z" footer -> exactly that version, which must be higher
# A breaking change is "type!:" / "type(scope)!:" or a "BREAKING CHANGE:" footer.
# Other types (docs, test, chore, build, ci, refactor, ...) and merges keep the version.
param([string]$Repository = (Join-Path $PSScriptRoot '../..'), [string]$Revision = 'HEAD')
$ErrorActionPreference = 'Stop'
# Unit and record separators keep subjects and bodies unambiguous.
$log = & git -c core.quotepath=off -C $Repository log --reverse --topo-order '--format=%s%x1f%b%x1e' $Revision
if ($LASTEXITCODE -ne 0) { throw "Cannot read the commit history of $Revision" }
$major = 0; $minor = 0; $patch = 0
foreach ($record in (($log -join "`n") -split [char]0x1e)) {
    $parts = $record.Trim("`r", "`n") -split [char]0x1f, 2
    $body = if ($parts.Count -gt 1) { $parts[1] } else { '' }
    if ($parts[0] -cnotmatch '^(?<type>[a-z]+)(\([^)]*\))?(?<bang>!)?: \S') { continue }
    $type = $Matches.type
    $breaking = [bool]$Matches.bang -or $body -cmatch '(?m)^BREAKING[ -]CHANGE: '
    if ($body -cmatch '(?m)^Release-As: (?<x>0|[1-9][0-9]*)\.(?<y>0|[1-9][0-9]*)\.(?<z>0|[1-9][0-9]*)\s*$') {
        $target = [int]$Matches.x, [int]$Matches.y, [int]$Matches.z
        $older = $target[0] -lt $major -or ($target[0] -eq $major -and ($target[1] -lt $minor -or ($target[1] -eq $minor -and $target[2] -le $patch)))
        if ($older) { throw "Release-As $($target -join '.') does not advance $major.$minor.$patch" }
        $major, $minor, $patch = $target
    }
    elseif ($breaking -and $major -gt 0) { ++$major; $minor = 0; $patch = 0 }
    elseif ($breaking -or $type -eq 'feat') { ++$minor; $patch = 0 }
    elseif ($type -eq 'fix' -or $type -eq 'perf') { ++$patch }
}
Write-Output "$major.$minor.$patch"
