param([string]$Compiler='C:/devkitPro/msys2/usr/bin/g++.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    [IO.Directory]::CreateDirectory((Join-Path $root 'build')) | Out-Null
    & $Compiler -std=c++23 -Wall -Wextra -Werror -Itests/container_stubs -Isphaira/include tests/container_tests.cpp sphaira/source/yati/container/xci.cpp sphaira/source/yati/container/nsp.cpp -o build/container-tests.exe
    if($LASTEXITCODE) {throw 'Container host-test compilation failed'}
    & ./build/container-tests.exe
    if($LASTEXITCODE) {throw 'Container host tests failed'}
    $yati=Get-Content sphaira/source/yati/yati.cpp -Raw
    $batch=Get-Content sphaira/include/nexus/batch.hpp -Raw
    $manager=Get-Content sphaira/source/nexus/manager.cpp -Raw
    foreach($type in @('Nsp','Xci')) {
        if($yati -notmatch "make_unique<container::$type>\(source, source_size\)") {throw "$type lost source-size bounds"}
    }
    if($batch -notmatch 'package::Supported' -or $yati -notmatch 'package::Classify') {throw 'UI and parser classifiers differ'}
    if($manager -notmatch 'InstallFromSource\(p,&source,path,cfg,size\)' -or $manager -notmatch 'run\(src,src.Size\(\)\)' -or $manager -notmatch 'InstallFromFile\(') {throw 'Source route missing'}
    Write-Output 'Format/routing source contracts passed (not a Horizon IPC or NCZ decompression test).'
} finally {Pop-Location}
