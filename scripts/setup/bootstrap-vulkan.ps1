# Build the official pinned Vulkan loader privately; no installer or global changes.
param([switch]$AllowDownloads)
$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $PSScriptRoot 'download-tools.ps1')
. (Join-Path $PSScriptRoot 'host-platform.ps1')
$lock=Get-Content -LiteralPath (Join-Path $root 'deps.lock.json') -Raw | ConvertFrom-Json
$tools=@($lock.vulkan_headers,$lock.vulkan_loader)
if($tools[0].version -ne $tools[1].version) {throw 'Vulkan source versions differ'}
$prefix=Join-Path $root ('.local/toolchains/vulkan-'+$tools[0].version)
[void][IO.Directory]::CreateDirectory($prefix)
$lease=[IO.File]::Open((Join-Path $prefix '.bootstrap.lock'),'OpenOrCreate','ReadWrite','None')
$archives=$null
try {
    $archives=Get-VerifiedToolArchives $tools (Join-Path $root '.local/cache/downloads') -AllowDownloads:$AllowDownloads
    $source=Join-Path $prefix 'source'
    [void][IO.Directory]::CreateDirectory($source)
    foreach($tool in $tools) {
        $folder=Join-Path $source $tool.prefix
        $receipt=Join-Path $folder '.x360n-sha256'
        if(Test-Path -LiteralPath $folder) {
            if(-not (Test-Path -LiteralPath $receipt) -or
               [IO.File]::ReadAllText($receipt).Trim() -ne $tool.sha256) {throw "Unverified Vulkan source: $folder"}
        } else {
            & tar -xzf $archives.archives[$tool.id] -C $source
            if($LASTEXITCODE) {throw 'Vulkan source extraction failed'}
            [IO.File]::WriteAllText($receipt,$tool.sha256)
        }
    }
    $headers=Join-Path $source $tools[0].prefix
    $loader=Join-Path $source $tools[1].prefix
    $project=@'
cmake_minimum_required(VERSION 3.25)
project(x360n_vulkan_build LANGUAGES C)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(UPDATE_DEPS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("@HEADERS@" headers)
add_subdirectory("@LOADER@" loader)
'@
    $project=$project.Replace('@HEADERS@',$headers.Replace('\','/')).Replace('@LOADER@',$loader.Replace('\','/'))
    $projectPath=Join-Path $prefix 'CMakeLists.txt'
    if(-not (Test-Path -LiteralPath $projectPath) -or [IO.File]::ReadAllText($projectPath) -ne $project) {
        [IO.File]::WriteAllText($projectPath,$project)
    }
    $build=Join-Path $prefix ('build-'+(Get-HostPlatform))
    & (Join-Path $root 'scripts/devshell.cmd') cmake -S $prefix -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
    if($LASTEXITCODE) {throw 'Vulkan loader configure failed'}
    & (Join-Path $root 'scripts/devshell.cmd') cmake --build $build --target vulkan --parallel 4
    if($LASTEXITCODE) {throw 'Vulkan loader build failed'}
    $library=Join-Path $build 'loader/loader/vulkan-1.lib'
    if(-not (Test-Path -LiteralPath $library)) {throw 'Vulkan import library missing'}
    $config='set(Vulkan_INCLUDE_DIR "'+$headers.Replace('\','/')+'/include" CACHE PATH "Pinned official Vulkan headers")'+"`n"+
        'set(Vulkan_LIBRARY "'+$library.Replace('\','/')+'" CACHE FILEPATH "Pinned official Vulkan loader import library")'+"`n"
    $configPath=Join-Path $prefix 'vulkan.cmake'
    if(-not (Test-Path -LiteralPath $configPath) -or [IO.File]::ReadAllText($configPath) -ne $config) {
        [IO.File]::WriteAllText($configPath,$config)
    }
    Write-Output "Vulkan $($tools[0].version): private headers and loader ready in $prefix"
} finally {
    if($archives) {foreach($held in $archives.leases) {$held.Dispose()}}
    $lease.Dispose()
}
