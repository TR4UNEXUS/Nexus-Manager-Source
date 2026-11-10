$ErrorActionPreference='Stop'
$appRoot=Split-Path $PSScriptRoot -Parent
$sdk=((Join-Path (Split-Path $appRoot -Parent) 'fw23-build-deps/libnx') -replace '\\','/')
$out=Join-Path $appRoot 'build/nexus-probe'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$previousDkp=$env:DEVKITPRO
try {
    $env:DEVKITPRO='C:/devkitPro'
    & C:/devkitPro/devkitA64/bin/aarch64-none-elf-gcc.exe -std=gnu11 -O2 -Wall -Wextra -Werror -D__SWITCH__ -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections -fdata-sections "-I$sdk/include" "-I$appRoot/sphaira/include" "-L$sdk/lib" '-specs=C:/devkitPro/libnx/switch.specs' '-Wl,--gc-sections' (Join-Path $PSScriptRoot 'vsd-probe/probe.c') -lnx -o (Join-Path $out 'VsdProbe.elf')
    if($LASTEXITCODE){throw 'Bundled probe compilation failed'}
    & C:/devkitPro/tools/bin/nacptool.exe --create 'USB Loader Diagnostics' 'TechRepairs4U' '0.2.0' (Join-Path $out 'VsdProbe.nacp')
    if($LASTEXITCODE){throw 'Probe NACP failed'}
    & C:/devkitPro/tools/bin/elf2nro.exe (Join-Path $out 'VsdProbe.elf') (Join-Path $out 'VsdProbe.nro') "--nacp=$(Join-Path $out 'VsdProbe.nacp')"
    if($LASTEXITCODE){throw 'Probe NRO failed'}
} finally {$env:DEVKITPRO=$previousDkp}
Write-Output 'Embedded USB diagnostics compiled.'
