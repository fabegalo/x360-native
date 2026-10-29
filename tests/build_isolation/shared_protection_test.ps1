param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
. "$Root/scripts/build/preparation-receipt.ps1"
. "$Root/scripts/build/incremental-state.ps1"
Initialize-ProductFingerprints
$scratch=Join-Path $Root ('.local/build/shared-protection-test-'+[Guid]::NewGuid().ToString('N'))
$directory=Join-Path $scratch 'producer'
[void][IO.Directory]::CreateDirectory($directory)
$file=Join-Path $directory 'input.lib'
[IO.File]::WriteAllText($file,'immutable producer')
$entries=@(Get-ReceiptEntries @($file))
$scope=[X360NProducts]::ProtectReceipt([string[]]@($entries.path),[string[]]@($entries.sha256))
try {
    if([X360NProducts]::ProtectedHash($file) -ne $entries[0].sha256){throw 'Ordinary producer path was not protected'}
    foreach($operation in @('write','rename','parent')) {
        $blocked=$false
        try {
            if($operation -eq 'write'){[IO.File]::WriteAllText($file,'changed bytes')}
            if($operation -eq 'rename'){[IO.File]::Move($file,"$file.moved")}
            if($operation -eq 'parent'){[IO.Directory]::Move($directory,"$directory.moved")}
        }catch{$blocked=$true}
        if(-not $blocked){throw "Producer exclusion failed: $operation"}
    }
    if(-not(Test-ReceiptEntries $entries) -or (Get-ReceiptHash $file) -ne $entries[0].sha256){throw 'Protected receipt disagrees with SHA-256'}
    $wrong=@([pscustomobject]@{path=$file;sha256=('0'*64)})
    if(Test-ReceiptEntries $wrong){throw 'Protected input accepted another expected digest'}
}finally{$scope.Dispose()}
if([X360NProducts]::ProtectedHash($file)){throw 'Approval escaped preparation scope'}
$time=[IO.File]::GetLastWriteTimeUtc($file)
[IO.File]::WriteAllText($file,'changed producer!')
[IO.File]::SetLastWriteTimeUtc($file,$time)
if(Test-ReceiptEntries $entries){throw 'Mutation after scope disposal reused approval'}
$refused=$false
try{$scope=[X360NProducts]::ProtectReceipt([string[]]@($entries.path),[string[]]@($entries.sha256));$scope.Dispose()}catch{$refused=$true}
if(-not $refused){throw 'Changed producer was pinned as valid'}
[IO.File]::WriteAllText($file,'handles released after failure')
Write-Output 'PASS: protected SHA reuse, write/file/directory replacement exclusion, wrong hashes, disposal and failed-pin cleanup'
