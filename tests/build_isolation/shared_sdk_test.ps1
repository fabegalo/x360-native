param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
. "$Root/scripts/build/preparation-receipt.ps1"
. "$Root/scripts/build/incremental-state.ps1"
. "$Root/scripts/build/shared-sdk.ps1"
$scratch = Join-Path $Root ('.local/build/shared-sdk-test-' + [Guid]::NewGuid().ToString('N'))
$generation = Join-Path $scratch '.local/build/shared-sdk/generation'
[void][IO.Directory]::CreateDirectory($generation)
$source = Join-Path $scratch 'source.cpp'
$pointer = Join-Path $scratch 'pointer.json'
$receiptFile = Join-Path $generation 'sdk.preparation.json'
$tool = Join-Path $generation 'rexglue.exe'
$dll = Join-Path $generation 'rexruntime.dll'
$export = Join-Path $generation 'sdk.cmake'
$targets = Join-Path $generation 'targets.cmake'
$identity = 'a' * 64
$checks = 0
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message }; $script:checks++ }
function Publish {
    Publish-PreparationReceipt $receiptFile @{schema=1;identity=$identity;inputs=@(Get-ReceiptEntries @($source));
        artifacts=@(Get-ReceiptEntries @($tool,$dll,$export,$targets));tool=$tool;producer=@($tool,$dll);globs=@()}
    Publish-PreparationReceipt $pointer @{build=$generation;sha256=(Get-ReceiptHash $receiptFile)}
}
function Rejected([string]$ExpectedIdentity=$identity) {
    try { $null = Read-SharedSdkGeneration $scratch $pointer $ExpectedIdentity; return $false } catch { return $true }
}
function ChangePreservingMetadata([string]$File) {
    $time = [IO.File]::GetLastWriteTimeUtc($File)
    $text = [IO.File]::ReadAllText($File)
    [IO.File]::WriteAllText($File, ('X' + $text.Substring(1)))
    [IO.File]::SetLastWriteTimeUtc($File,$time)
}
$sdk=Join-Path $scratch 'sdk'
$owned=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[void]$owned.Add((Join-Path $sdk 'runtime.cpp'))
Assert-SharedSdkSources @(@{file=(Join-Path $sdk 'runtime.cpp');output='runtime.obj'},@{file=(Join-Path $generation 'generated.cpp');output='generated.obj'}) $sdk $generation $owned
$foreign=$false
try{Assert-SharedSdkSources @(@{file=(Join-Path $sdk 'out/guest.cpp');output='guest.obj'}) $sdk $generation $owned}catch{$foreign=$true}
Assert $foreign 'Unidentified generated SDK guest source entered generic cache'
$foreign=$false
try{Assert-SharedSdkInputs $scratch @(@{path=(Join-Path $scratch 'titles/guest/generated.h')})}catch{$foreign=$true}
Assert $foreign 'Title-specific forced header entered generic cache'
Assert-SharedSdkInputs $scratch @(@{path=(Join-Path $sdk 'runtime.h')},@{path=(Join-Path $generation 'generated.h')})
$foreign=$false
try{Assert-SharedSdkSources @(@{file=(Join-Path $scratch 'titles/guest.cpp');output='guest.obj'}) $sdk $generation}catch{$foreign=$true}
Assert $foreign 'Guest/title translation unit entered generic cache'
$foreign=$false
try{Assert-SharedSdkSources @(@{file=(Join-Path $scratch 'sdk-other/runtime.cpp');output='runtime.obj'}) $sdk $generation}catch{$foreign=$true}
Assert $foreign 'SDK basename prefix accepted an external translation unit'
foreach ($file in @($source,$tool,$dll,$export,$targets)) { [IO.File]::WriteAllText($file, 'original bytes') }
Assert (Rejected) 'Interrupted generation without receipt accepted'
Publish
$ready = Read-SharedSdkGeneration $scratch $pointer $identity
Assert ($ready.build -eq $generation -and $ready.tool -eq $tool) 'Verified generation rejected'
Assert (Rejected ('b'*64)) 'SDK/compiler/flags/dependency identity change accepted'
foreach ($file in @($source,$tool,$dll,$export,$targets)) {
    ChangePreservingMetadata $file
    Assert (Rejected) "Same-size/timestamp corruption accepted: $file"
    [IO.File]::WriteAllText($file,'original bytes')
}
ChangePreservingMetadata $receiptFile
Assert (Rejected) 'Corrupted receipt accepted through pointer'
Publish
[IO.File]::Delete($tool)
Assert (Rejected) 'Incomplete producer accepted'
[IO.File]::WriteAllText($tool,'original bytes')
Publish
$receipt = [IO.File]::ReadAllText($receiptFile) | ConvertFrom-Json
$receipt.producer = @((Join-Path $scratch 'unverified.dll'))
Publish-PreparationReceipt $receiptFile $receipt
Publish-PreparationReceipt $pointer @{build=$generation;sha256=(Get-ReceiptHash $receiptFile)}
Assert (Rejected) 'Producer outside authenticated artifacts accepted'
Publish
Publish-PreparationReceipt $pointer @{build=$scratch;sha256=(Get-ReceiptHash $receiptFile)}
Assert (Rejected) 'Generation escaped private cache boundary'
Publish
# Interrupted publication must leave the previous verified generation usable.
[IO.File]::WriteAllText("$pointer.interrupted.tmp", '{partial')
Assert (-not (Rejected)) 'Unpublished partial generation invalidated ready cache'
$consumer = Join-Path $scratch 'consumer.dll'
[IO.File]::Copy($dll,$consumer)
[IO.File]::WriteAllText($consumer,'consumer mutation')
Assert (-not (Rejected)) 'Private consumer DLL shared writable cache state'
Write-Output "PASS: $checks shared SDK integrity, identity, interruption and isolation checks; evidence: $scratch"
