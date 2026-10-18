# Original 17559 analysis + native registration only. Never calls guest code,
# starts UI, edits original inputs/profiles, or publishes preparation receipts.
param(
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$ModuleProbe,
    [Parameter(Mandatory)][string]$ContentTool,
    [Parameter(Mandatory)][string]$NativeDll,
    [Parameter(Mandatory)][string]$Output,
    [string]$Root
)
$ErrorActionPreference='Stop'
if(-not $Root){$Root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path}
. "$Root/scripts/build/preparation-receipt.ps1"
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
$Output=[IO.Path]::GetFullPath($Output)
Assert (-not (Test-Path $Output)) 'Preserve the previous diagnostic output'
[void][IO.Directory]::CreateDirectory($Output)
function Run([string]$Exe,[string[]]$Arguments,[string]$Name){
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=(Resolve-Path $Exe).Path; $info.WorkingDirectory=$Output
    $info.Arguments=($Arguments | ForEach-Object {'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}) -join ' '
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $p=[Diagnostics.Process]::new();$p.StartInfo=$info
    try{
        [void]$p.Start();$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
        if(-not $p.WaitForExit(60000)){$p.Kill();throw "Diagnostic exceeded 60 seconds: $Name"}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText("$Output/$Name.log",$text)
        Assert ($p.ExitCode -eq 0) "$Name failed: exit $($p.ExitCode); see $Output"
        $text
    } finally {if(-not $p.HasExited){$p.Kill()};$p.Dispose()}
}
$gameRoot=Join-Path $Root '.local/inputs/dashboards/17559/runtime'
$original=Join-Path $gameRoot 'system/signin.xex'
$originalSha=Get-ReceiptHash $original
Assert ($originalSha -eq '86695c6c36250cc4b4addb7ccc07d15d54492b352c979cf5c0934804109a6130') 'Requires the identified original Retail 17559 sign-in module'
$layout=Run $ModuleProbe @($original,(Resolve-Path $NativeDll).Path,'auto') 'native-layout'
$base=[Convert]::ToUInt32([regex]::Match($layout,'image_base=([0-9a-fA-F]+)').Groups[1].Value,16)
$callback=[Convert]::ToUInt32('90116C88',16)
$config="project_name='signin_contract'`nfile_path='$($original.Replace('\','/'))'`nout_directory_path='unused'`n"+[IO.File]::ReadAllText("$Root/titles/dash_17559/analysis/signin_17559.toml")
[IO.File]::WriteAllText("$Output/analysis.toml",$config)
$null=Run $Probe @("$Output/analysis.toml","$Output/graph.tsv",'90116C88','90116A08') 'analysis'
$graph=[IO.File]::ReadAllLines("$Output/graph.tsv")
$query=@($graph | Where-Object {$_ -match '^query\t90116C88\t'})[0] -split "`t"
$function=@($graph | Where-Object {$_ -match '^function\t90116C88\t'})
$dump=Join-Path $Output 'image.bin'
try{
    $null=Run $ContentTool @('dump-image',$gameRoot,'system/signin.xex',$dump) 'original-image'
    $image=[IO.File]::ReadAllBytes($dump)
    $offset=[int]($callback-$base)
    Assert ($offset -ge 0 -and $offset+12 -le $image.Length) 'Callback outside original image'
    $body=[byte[]]$image[$offset..($offset+11)]
    Assert (($body[8] -eq 0x4e -and $body[9] -eq 0x80 -and $body[10] -eq 0 -and $body[11] -eq 0x20)) 'Three-word original leaf must end in blr'
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$bodySha=([BitConverter]::ToString($sha.ComputeHash($body))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    # Full original SHA binds the cmplwi cr6,r11,12/bgt sequence and this
    # table/base. Verify the actual 13 slots do not target the adjacent callback.
    $table=[Convert]::ToUInt32('901027F8',16);$caseBase=[Convert]::ToUInt32('90116A5C',16)
    $targets=@(0..12 | ForEach-Object {[uint32]([uint64]$caseBase+4*[uint64]$image[[int]($table-$base)+$_])} | Sort-Object -Unique)
    Assert (@($targets | Where-Object {$_ -ge $callback}).Count -eq 0) 'Original bounded switch reaches the proposed callback boundary'
    $latin=[Text.Encoding]::GetEncoding(28591)
    $text=$latin.GetString($image);$needle=$latin.GetString([byte[]]@(0x90,0x11,0x6c,0x88))
    $references=@();$at=$text.IndexOf($needle)
    while($at -ge 0){
        $address=[uint64]$base+$at
        if(($at%4) -eq 0){
            foreach($line in $graph | Where-Object {$_ -match '^section\t'}){
                $section=$line -split "`t";$start=[Convert]::ToUInt32($section[2],16)
                if($section[4] -eq '0' -and $address -ge $start -and $address+4 -le [uint64]$start+[uint64]$section[3]){$references+=@($address.ToString('x8'));break}
            }
        }
        $at=$text.IndexOf($needle,$at+1)
    }
    Assert ($references.Count -gt 0) 'No original data reference to the callback'
    $registered=$query[4] -eq '1' -and $query[3] -eq '90116C88'
    @{original_sha256=$originalSha;body_sha256=$bodySha;data_references=$references;table_slots=13;table_targets=@($targets | ForEach-Object {$_.ToString('x8')});entry_registered=$registered;guest_execution=$false} |
        ConvertTo-Json -Depth 5 | Set-Content "$Output/result.json"
    Assert $registered 'Original virtual callback is swallowed by the preceding dispatch'
    Assert ($function.Count -eq 1 -and [int](($function[0] -split "`t")[3]) -eq 12) 'Callback must own exactly its original three instructions'
    $dispatcher=@($graph | Where-Object {$_ -match '^function\t90116A08\t'})[0] -split "`t"
    Assert ([uint64]([Convert]::ToUInt32('90116A08',16))+[uint64]$dispatcher[3] -le $callback) 'Dispatcher still owns the independent callback'
    $null=Run $ModuleProbe @($original,(Resolve-Path $NativeDll).Path,'auto','90116C88') 'native-callback'
    Assert ((Get-ReceiptHash $original) -eq $originalSha) 'Original input changed'
    Write-Output 'PASS: original hash/data references/13-slot boundary, independent three-word callback and native registration; no guest execution'
} finally {if([IO.File]::Exists($dump)){[IO.File]::Delete($dump)}}
