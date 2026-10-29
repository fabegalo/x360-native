function Open-VerifiedToolArchive([string]$File, [string]$Expected) {
    $stream = [IO.File]::Open($File,'Open','Read','Read')
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $actual = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()
        if ($actual -ne $Expected) { throw "Tool archive checksum mismatch: $File" }
        $stream.Position = 0
        return $stream
    } catch { $stream.Dispose(); throw }
    finally { $sha.Dispose() }
}
function Get-VerifiedToolArchives($Tools, [string]$Cache, [switch]$AllowDownloads, [ValidateRange(1,4)][int]$Jobs=4) {
    [void][IO.Directory]::CreateDirectory($Cache)
    $leases = [Collections.Generic.List[IDisposable]]::new()
    $running = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Queue[object]]::new()
    $archives = @{}
    $success = $false
    try {
        foreach ($tool in $Tools) {
            if ([IO.Path]::GetFileName($tool.asset) -ne $tool.asset -or $tool.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid pinned tool archive identity' }
            $archive = Join-Path $Cache $tool.asset
            if ([IO.File]::Exists($archive)) {
                $leases.Add((Open-VerifiedToolArchive $archive $tool.sha256))
                $archives[$tool.id] = $archive
                continue
            }
            $part = "$archive.part"
            # A transfer can finish immediately before its parent is cancelled.
            if ([IO.File]::Exists($part) -and ([IO.FileInfo]::new($part)).Length -eq $tool.size) {
                try {
                    $complete = Open-VerifiedToolArchive $part $tool.sha256
                    $complete.Dispose()
                    [IO.File]::Move($part,$archive)
                    $leases.Add((Open-VerifiedToolArchive $archive $tool.sha256))
                    $archives[$tool.id] = $archive
                    continue
                } catch {
                    # A full-length corrupt prefix cannot be repaired by a
                    # Range request at EOF. Retain it and restart with consent.
                    if ([IO.File]::Exists($part)) {
                        [IO.File]::Move($part,"$archive.rejected-$([Guid]::NewGuid().ToString('N'))")
                    }
                }
            }
            if (-not $AllowDownloads) { throw "Download consent required: $($tool.url) ($($tool.size) bytes)" }
            $pending.Enqueue($tool)
        }
        while ($pending.Count -or $running.Count) {
            while ($pending.Count -and $running.Count -lt $Jobs) {
                $tool = $pending.Dequeue()
                $archive = Join-Path $Cache $tool.asset
                Write-Host "X360N_STAGE download $($tool.id) $($tool.size) bytes (resume enabled)"
                $arguments = @('--silent','--show-error','--fail','--location','--retry','3','--continue-at','-',
                    '--connect-timeout','30','--speed-limit','262144','--speed-time','30','--max-time','600','--output',"$archive.part",$tool.url)
                $quoted = foreach ($argument in $arguments) { '"' + [regex]::Replace([regex]::Replace([string]$argument,'(\\*)"','$1$1\"'),'(\\+)$','$1$1') + '"' }
                $info = [Diagnostics.ProcessStartInfo]::new()
                $info.FileName = "$env:SystemRoot/System32/curl.exe"
                $info.Arguments = $quoted -join ' '
                $info.UseShellExecute = $false; $info.CreateNoWindow = $true
                $info.RedirectStandardError = $true
                $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
                if (-not $process.Start()) { $process.Dispose(); throw 'Cannot start pinned tool download' }
                $running.Add(@{process=$process;error=$process.StandardError.ReadToEndAsync();tool=$tool;archive=$archive;watch=[Diagnostics.Stopwatch]::StartNew()})
            }
            foreach ($item in @($running.ToArray())) {
                if (-not $item.process.WaitForExit(100)) {
                    if ($item.watch.Elapsed.TotalSeconds -gt 660) { throw "Tool download exceeded 660 seconds: $($item.tool.id)" }
                    continue
                }
                $errorText = $item.error.GetAwaiter().GetResult()
                if ($item.process.ExitCode) { throw "Download interrupted (curl $($item.process.ExitCode)): $($item.tool.url). Partial bytes preserved. $errorText" }
                try {
                    $verified = Open-VerifiedToolArchive "$($item.archive).part" $item.tool.sha256
                    $verified.Dispose()
                } catch {
                    # A corrupt completed prefix must not poison every future
                    # resume. Preserve it for diagnosis and retry from zero.
                    [IO.File]::Move("$($item.archive).part", "$($item.archive).rejected-$([Guid]::NewGuid().ToString('N'))")
                    throw
                }
                [IO.File]::Move("$($item.archive).part",$item.archive)
                $leases.Add((Open-VerifiedToolArchive $item.archive $item.tool.sha256))
                $archives[$item.tool.id] = $item.archive
                $item.process.Dispose()
                [void]$running.Remove($item)
            }
        }
        $success = $true
        return @{archives=$archives;leases=$leases}
    } finally {
        foreach ($item in $running) {
            if (-not $item.process.HasExited) {
                try { $item.process.Kill() } catch [InvalidOperationException] { }
                [void]$item.process.WaitForExit(5000)
            }
            $item.process.Dispose()
        }
        if (-not $success) { foreach ($lease in $leases) { $lease.Dispose() } }
    }
}
