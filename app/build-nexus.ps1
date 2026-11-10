param([switch]$ConfigureOnly, [ValidateRange(1,2)][int]$Jobs = 1, [string]$WslDistribution='MM-HATS-Build')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if (!(Test-Path -LiteralPath 'C:/devkitPro/msys2/usr/bin/cmake.exe')) {
    $wslPathText = @(& wsl.exe -d $WslDistribution -- wslpath -u ($PSScriptRoot -replace '\\','/'))
    if ($LASTEXITCODE -or !$wslPathText.Count) { throw 'Could not locate project in WSL' }
    $linuxApp = ($wslPathText -join "`n").Trim()
    $arguments = @('-d',$WslDistribution,'--','env',"NEXUS_BUILD_JOBS=$Jobs",'sh',"$linuxApp/build-nexus-wsl.sh")
    if ($ConfigureOnly) { $arguments += '--configure-only' }
    & wsl.exe @arguments
    if ($LASTEXITCODE) { throw 'WSL Nexus Manager build failed' }
    exit 0
}
# Probe-free recovery build: USB game/homebrew support does not require it.
$env:DEVKITPRO = '/opt/devkitpro'
$env:DEVKITA64 = '/opt/devkitpro/devkitA64'
$workspaceWork = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$privateNx = Join-Path $workspaceWork 'fw23-build-deps/libnx'
if (Test-Path -LiteralPath $privateNx) { $env:LIBNX = (& C:/devkitPro/msys2/usr/bin/cygpath.exe -u $privateNx).Trim() }
$graphics = Join-Path $workspaceWork 'usb-cnx-final/graphics-sdk'
if(Test-Path -LiteralPath $graphics) { $env:NEXUS_GRAPHICS = (& C:/devkitPro/msys2/usr/bin/cygpath.exe -u $graphics).Trim() }
$env:PATH = 'C:/devkitPro/msys2/usr/bin;C:/devkitPro/devkitA64/bin;C:/devkitPro/tools/bin;C:/devkitPro/portlibs/switch/bin;' + $env:PATH
$opts = @('-S','.', '-B','build/NexusD10','-G','Ninja','-DCMAKE_TOOLCHAIN_FILE=cmake/NexusSwitch.cmake','-DCMAKE_BUILD_TYPE=MinSizeRel','-DLTO=OFF',
 '-DENABLE_LIBUSBHSFS=OFF','-DENABLE_LIBUSBDVD=OFF','-DENABLE_NTFS=OFF','-DENABLE_EXT4=OFF',
 '-DENABLE_FTPSRV=OFF','-DENABLE_LIBHAZE=OFF','-DENABLE_DEVOPTAB_NFS=OFF','-DENABLE_DEVOPTAB_SMB2=OFF',
 '-DENABLE_DEVOPTAB_SFTP=OFF','-DENABLE_DEVOPTAB_WEBDAV=OFF','-DENABLE_DEVOPTAB_FTP=OFF',
 '-DENABLE_DEVOPTAB_HTTP=OFF','-DENABLE_AUDIO_MP3=OFF','-DENABLE_AUDIO_OGG=OFF',
 '-DENABLE_AUDIO_WAV=OFF','-DENABLE_AUDIO_FLAC=OFF',
 '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON','-DFETCHCONTENT_FULLY_DISCONNECTED=ON','-DNEXUS_BUNDLED_PROBE=OFF','-Dsphaira_VERSION_OVERRIDE=0.2.0-test3',
 '-DNEXUS_PRIVATE_KEY_INCLUDE_DIR=')
& cmake @opts
if ($LASTEXITCODE) { throw 'Configure failed' }
if ($ConfigureOnly) { exit 0 }
$minimumFreeKb = if ($Jobs -eq 1) { 1500000 } else { 2300000 }
if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt $minimumFreeKb) { throw "Need at least $minimumFreeKb KB free RAM for $Jobs compiler job(s)" }
$job = Start-Process -FilePath 'C:/devkitPro/msys2/usr/bin/cmake.exe' -ArgumentList "--build build/NexusD10 --parallel $Jobs" -WindowStyle Hidden -PassThru -RedirectStandardOutput build-output.log -RedirectStandardError build-error.log
$null = $job.Handle
$job.PriorityClass = 'BelowNormal'
while (-not $job.HasExited) {
  if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 870400) {
    & taskkill /PID $job.Id /T /F
    throw 'Build stopped to protect available RAM'
  }
  Start-Sleep -Seconds 2
  $job.Refresh()
}
$job.WaitForExit()
if ($null -eq $job.ExitCode -or $job.ExitCode -ne 0) { throw "Build failed (exit $($job.ExitCode)); see build-error.log and build-output.log" }
Write-Output 'Nexus Manager key-free build complete: build/NexusD10/switch/nexusmanager/NexusManager.nro'
