# Shared environment access excludes running sessions/tool installation, while
# independent builds may coexist. Exclusive leases cover the Ninja database and
# the title's source-tree codegen outputs (also shared by alternate BuildDirs).
function Enter-BuildLease([string]$Root, [string]$Build, [string]$Title) {
    $handles = [Collections.Generic.List[IDisposable]]::new()
    try {
        [void][IO.Directory]::CreateDirectory((Join-Path $Root '.local/preparation'))
        $handles.Add([IO.File]::Open((Join-Path $Root '.local/environment.lock'), 'OpenOrCreate', 'ReadWrite', 'ReadWrite'))
        foreach ($pending in @('.local/payload-update/pending', '.local/dependency-update.pending')) {
            if (Test-Path -LiteralPath (Join-Path $Root $pending)) { throw 'Atualizacao interrompida. Reabra o aplicativo para concluir; seus dados foram preservados.' }
        }
        $paths = @((Join-Path $Build '.x360n-build.lock'))
        if ($Title) { $paths += Join-Path $Root ".local/preparation/$Title.lock" }
        foreach ($path in $paths) { $handles.Add([IO.File]::Open($path, 'OpenOrCreate', 'ReadWrite', 'None')) }
        return ,$handles
    } catch {
        foreach ($handle in $handles) { $handle.Dispose() }
        throw "Build unavailable: $Build ($Title). Nenhum artefato foi alterado. $($_.Exception.Message)"
    }
}
