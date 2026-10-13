param([string]$EventName, [string]$Started)
$event = [Threading.EventWaitHandle]::OpenExisting($EventName)
try {
    [IO.File]::WriteAllText($Started, 'build owns its outputs')
    if (-not $event.WaitOne(30000)) { exit 1 }
} finally { $event.Dispose() }
