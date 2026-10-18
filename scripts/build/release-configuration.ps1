# Production preparation and packaging must not reuse a developer configuration.
function Test-ReleaseBuildConfiguration([string]$Build) {
    $file = Join-Path $Build 'CMakeCache.txt'
    if (-not [IO.File]::Exists($file)) { return $false }
    $entries = [regex]::Matches([IO.File]::ReadAllText($file), '(?m)^CMAKE_BUILD_TYPE:[^=\r\n]+=([^\r\n]*)\r?$')
    return $entries.Count -eq 1 -and $entries[0].Groups[1].Value -ceq 'Release'
}
function Assert-ReleaseCompileCommands($Commands, [switch]$NinjaGraph) {
    $count = 0
    # Windows PowerShell 5.1 emits a decoded JSON array as one pipeline item;
    # @(... | ConvertFrom-Json) can therefore contain a nested array.
    foreach ($entry in @($Commands | ForEach-Object { $_ })) {
        # Ninja's full compdb includes phony/custom edges whose first input can
        # be a .cpp. Only object/PCH producers are compiler evidence there.
        if ($NinjaGraph -and $entry.output -notmatch '\.(?:obj|o|pch|gch)$') { continue }
        if ($entry.file -notmatch '\.(?:c|cc|cpp|cxx|c\+\+|m|mm)$') { continue }
        $count++
        $command = $entry.command
        if (-not $command -or
            $command -notmatch '(?:^|\s)-O3(?:\s|$)' -or
            $command -notmatch '(?:^|\s)-DNDEBUG(?:=1)?(?:\s|$)' -or
            $command -match '(?:^|\s)(?:-O(?:0|1|2|4|s|z|g|fast)|[-/]UNDEBUG|[-/]D_DEBUG(?:=\S+)?|[-/]D_ITERATOR_DEBUG_LEVEL=[12]|/M[DT]d)(?:\s|$)' -or
            $command -match '(?i)(?:libcpmtd|libcmtd|msvcrtd|ucrtd|vcruntimed)(?:\.lib)?(?:\s|$)') {
            throw "Non-Release compiler configuration: $($entry.file)"
        }
    }
    if (-not $count) { throw 'Release compiler evidence is empty' }
}
