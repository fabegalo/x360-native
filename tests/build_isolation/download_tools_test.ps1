param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
. "$Root/scripts/setup/download-tools.ps1"
$scratch=Join-Path $Root ('.local/build/download-tools-test-'+[Guid]::NewGuid().ToString('N'))
$cache=Join-Path $scratch 'cache'
[void][IO.Directory]::CreateDirectory($cache)
$tools=@()
foreach ($index in 1..4) {
    $source=Join-Path $scratch "source-$index.bin"
    [IO.File]::WriteAllBytes($source,[Text.Encoding]::UTF8.GetBytes(('payload-'+$index+';')*10000))
    $tools+= [pscustomobject]@{id="tool-$index";asset="tool-$index.zip";size=([IO.FileInfo]::new($source)).Length;
        sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant();url=([Uri]$source).AbsoluteUri}
}
$refused=$false
try { $null=Get-VerifiedToolArchives $tools $cache } catch { $refused=$_.Exception.Message -like '*consent required*' }
if (-not $refused -or @(Get-ChildItem -LiteralPath $cache -File).Count) { throw 'Download started without consent' }
# Resume an interrupted prefix using the real system curl, without network I/O.
[IO.File]::WriteAllBytes((Join-Path $cache 'tool-1.zip.part'),[Text.Encoding]::UTF8.GetBytes('payload-1;'))
$ready=Get-VerifiedToolArchives $tools $cache -AllowDownloads
try {
    if ($ready.archives.Count -ne 4) { throw 'Parallel batch incomplete' }
    foreach($tool in $tools) {
        if((Get-FileHash -LiteralPath $ready.archives[$tool.id]).Hash.ToLowerInvariant() -ne $tool.sha256){throw 'Resumed archive differs'}
        $blocked=$false
        try { $handle=[IO.File]::Open($ready.archives[$tool.id],'Open','Write','ReadWrite'); $handle.Dispose() } catch { $blocked=$true }
        if(-not $blocked){throw 'Verified archive can change during extraction'}
    }
} finally { foreach($lease in $ready.leases){$lease.Dispose()} }
$cached=Get-VerifiedToolArchives $tools $cache
foreach($lease in $cached.leases){$lease.Dispose()}
# A complete .part is authenticated and published with no download consent.
[IO.File]::Move((Join-Path $cache 'tool-1.zip'),(Join-Path $cache 'tool-1.zip.part'))
$cached=Get-VerifiedToolArchives $tools $cache
foreach($lease in $cached.leases){$lease.Dispose()}
$part=Join-Path $cache 'tool-2.zip.part'
[IO.File]::Move((Join-Path $cache 'tool-2.zip'),$part)
[IO.File]::WriteAllBytes($part,[byte[]]::new($tools[1].size))
$cached=Get-VerifiedToolArchives $tools $cache -AllowDownloads
foreach($lease in $cached.leases){$lease.Dispose()}
if(-not @(Get-ChildItem $cache -Filter 'tool-2.zip.rejected-*').Count){throw 'Corrupt full-length prefix was not retained'}
$wrong=[pscustomobject]@{id='wrong';asset='wrong.zip';size=$tools[0].size;sha256=('0'*64);url=$tools[0].url}
$rejected=$false
try {$null=Get-VerifiedToolArchives @($wrong) $cache -AllowDownloads} catch {$rejected=$_.Exception.Message -like '*checksum mismatch*'}
if(-not $rejected -or [IO.File]::Exists((Join-Path $cache 'wrong.zip')) -or -not @(Get-ChildItem $cache -Filter 'wrong.zip.rejected-*').Count){throw 'Checksum failure published or lost corrupt bytes'}
[IO.File]::WriteAllText((Join-Path $cache 'tool-1.zip'),'corrupt cache')
$rejected=$false
try {$null=Get-VerifiedToolArchives $tools $cache} catch {$rejected=$_.Exception.Message -like '*checksum mismatch*'}
if(-not $rejected){throw 'Corrupted cache accepted'}
Write-Output 'PASS: consent, four-tool batch, real curl resume, hash refusal, protected extraction, cached reuse and complete-part recovery'
