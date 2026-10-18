# Opt-in inspection of real prepared modules. No guest function or title entry
# point is called, and this test never creates/changes a preparation receipt.
param(
    [Parameter(Mandatory)][string]$Title,
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$Output,
    [string]$Root
)
$ErrorActionPreference='Stop'
if(-not $Root){$Root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path}
. "$Root/scripts/build/preparation-receipt.ps1"
. "$Root/scripts/build/incremental-state.ps1"
. "$Root/scripts/build/build-lease.ps1"
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
Assert ($Title -match '^[a-z][a-z0-9_]*$') 'Invalid title'
$Build=(Resolve-Path $Build).Path
$Probe=(Resolve-Path $Probe).Path
$Output=[IO.Path]::GetFullPath($Output)
Assert (-not (Test-Path $Output)) 'Preserve the previous diagnostic output'
[void][IO.Directory]::CreateDirectory($Output)
$project=Join-Path $Root ".local/aot/$Title"
$manifest=Join-Path $project "${Title}_manifest.toml"
$exe=Join-Path $Build "$Title.exe"
$receiptFile="$exe.preparation.json"
$lease=Enter-BuildLease $Root $Build $Title
function Invoke-Probe([string]$Original,[string]$Native,[string]$Entry,[string]$Name){
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$Probe; $info.WorkingDirectory=$Build
    $info.Arguments=(@($Original,$Native,$Entry) | ForEach-Object {
        '"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
    }) -join ' '
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $p=[Diagnostics.Process]::new(); $p.StartInfo=$info
    try{
        [void]$p.Start()
        $stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
        if(-not $p.WaitForExit(20000)){$p.Kill();throw "Module probe exceeded 20 seconds: $Name"}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $Output "$Name.log"),$text)
        [pscustomobject]@{code=$p.ExitCode;text=$text}
    } finally {if(-not $p.HasExited){$p.Kill()};$p.Dispose()}
}
try{
    Assert (-not (Test-Path "$Root/.local/preparation/$Title.incomplete")) 'Incomplete preparation'
    $r=Get-PreviousReceipt $receiptFile $Title $manifest $exe
    Assert ($null -ne $r) 'No valid preparation receipt identity/inventory'
    Assert (Test-ReceiptEntries @($r.inputs+$r.artifacts)) 'Preparation bytes differ'
    Assert (Test-ReceiptGlobs $r.globs) 'Preparation inventory differs'
    $head=(& git -C "$Root/external/rexglue-sdk" rev-parse HEAD).Trim()
    Assert ($LASTEXITCODE -eq 0 -and $head -eq $r.sdk_commit) 'SDK revision differs'
    $before=Get-ReceiptHash $receiptFile
    $modules=[Collections.Generic.List[object]]::new()
    $module=$null
    foreach($line in [IO.File]::ReadAllLines($manifest)){
        if($line -match '^\[\[modules\]\]$'){$module=@{};$modules.Add($module)}
        elseif($module -ne $null -and $line -match '^\s*(file_path|out_directory_path)\s*=\s*([''"])(.*?)\2\s*$'){$module[$matches[1]]=$matches[3]}
    }
    Assert ($modules.Count -gt 0) 'No declared DLL modules'
    $results=@()
    foreach($module in $modules){
        $original=Resolve-BuildPath $project $module.file_path
        $generated=Resolve-BuildPath $project $module.out_directory_path
        # Same derivation as pinned ProjectRecompiler::DeriveTargetNameFromFilePath.
        $name=[IO.Path]::GetFileNameWithoutExtension($original).Replace('.','_').Replace(' ','_')
        $native=Join-Path $Build "${Title}_$name.dll"
        $registration=Join-Path $generated "${Title}_register.cpp"
        Assert ($original -in @($r.inputs.path) -and $registration -in @($r.inputs.path)) "Unauthenticated module inputs: $name"
        Assert ($native -in @($r.artifacts.path)) "Unauthenticated native module: $name"
        $addresses=[string[]]@([regex]::Matches([IO.File]::ReadAllText($registration),'SetFunction\(0x([0-9A-Fa-f]{8}),') | ForEach-Object {$_.Groups[1].Value.ToLowerInvariant()})
        [Array]::Sort($addresses,[StringComparer]::Ordinal)
        $canonical=if($addresses.Count){($addresses -join "`n")+"`n"}else{''}
        $expected=Get-TextDigest $canonical
        $result=Invoke-Probe $original $native 'auto' $name
        Assert ($result.code -eq 0) "Native module ABI failed: $name (exit $($result.code)); see $Output"
        Assert ($result.text.Contains("mapping_addresses_sha256=$expected")) "Linked registration differs from generated source: $name"
        $count=[int]([regex]::Match($result.text,'(?m)^mappings=(\d+)').Groups[1].Value)
        Assert ($count -eq $addresses.Count) "Registration count differs: $name"
        Assert ($result.text.Contains('guest_execution=false')) 'Probe must never execute guest functions'
        $results+=@{name=$name;original=$original;native=$native;mappings=$count;addresses_sha256=$expected;native_sha256=(Get-ReceiptHash $native)}
    }
    $nativeModules=@($r.artifacts | Where-Object {[IO.Path]::GetFileName($_.path).StartsWith("${Title}_") -and $_.path.EndsWith('.dll')})
    Assert ($nativeModules.Count -eq $modules.Count) 'Native DLL inventory differs from module declarations'
    $negative=Invoke-Probe $results[0].original $results[0].native '1' 'negative-entry'
    Assert ($negative.code -eq 4) 'Wrong original entry identity accepted'
    Assert ((Get-ReceiptHash $receiptFile) -eq $before) 'Probe changed the receipt'
    Assert (Test-ReceiptEntries @($r.inputs+$r.artifacts)) 'Prepared products changed during inspection'
    @{schema=1;title=$Title;guest_execution=$false;sdk_commit=$head;receipt_sha256=$before;exe_sha256=(Get-ReceiptHash $exe);probe_sha256=(Get-ReceiptHash $Probe);collector_sha256=(Get-ReceiptHash $PSCommandPath);modules=$results;wrong_entry_rejected=$true} |
        ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Output 'result.json')
    Write-Output "PASS: $($modules.Count) native DLL identities, registrations and address sets; wrong entry rejected; receipt unchanged; no guest execution"
} finally {foreach($handle in $lease){$handle.Dispose()}}
