param([Parameter(Mandatory=$true)][string]$SdRoot)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($SdRoot).TrimEnd('\','/')+'\'
$packageRoot=Split-Path $PSScriptRoot -Leaf
# In the runtime ZIP this script is at the root; in source it lives in tools.
$source=Join-Path $PSScriptRoot 'switch/usbloader/USBLoader.nro'
$manifestPath=Join-Path $PSScriptRoot 'BUILD-MANIFEST.json'
if(-not(Test-Path -LiteralPath $source)){throw 'Run Apply-App.ps1 from the extracted runtime ZIP, beside its switch folder.'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$target=Join-Path $root 'switch/usbloader/USBLoader.nro'
$firmware=Join-Path $root 'atmosphere/package3'
if(-not(Test-Path -LiteralPath $target) -or -not(Test-Path -LiteralPath (Join-Path $root 'bootloader/hekate_ipl.ini'))){throw 'This does not look like the existing Switch SD. Nothing changed.'}
if((Get-FileHash -LiteralPath $firmware).Hash.ToLowerInvariant() -ne $manifest.required_package3_sha256){throw 'Expected the paired Nexus USB Stability T1 firmware. No firmware or app changed.'}
if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $manifest.nro_sha256){throw 'Package NRO hash mismatch.'}
$before=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
if($before -eq $manifest.nro_sha256){Write-Output 'USB Loader 0.6.9 is already installed and verified.';exit 0}
$stamp=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$backup=Join-Path $root ('switch/usbloader/app-backups/'+$stamp)
[IO.Directory]::CreateDirectory($backup)|Out-Null
$backupFile=Join-Path $backup 'USBLoader.nro'
Copy-Item -LiteralPath $target -Destination $backupFile
if((Get-FileHash -LiteralPath $backupFile).Hash.ToLowerInvariant() -ne $before){throw 'Backup verification failed. Existing app untouched.'}
$stage=Join-Path $root ('switch/usbloader/USBLoader.nro.'+$stamp+'.staged')
Copy-Item -LiteralPath $source -Destination $stage
if((Get-FileHash -LiteralPath $stage).Hash.ToLowerInvariant() -ne $manifest.nro_sha256){throw 'Staged app verification failed. Existing app untouched.'}
if((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $before){throw 'App changed during staging. It was not replaced.'}
Copy-Item -LiteralPath $stage -Destination $target -Force
if((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $manifest.nro_sha256){
    Copy-Item -LiteralPath $backupFile -Destination $target -Force
    throw 'App verification failed; restored the previous NRO. Keep the backup.'
}
# Delete only the exact disposable staged NRO after verifying the installed copy.
Remove-Item -LiteralPath $stage
$state=[ordered]@{version='0.6.9';target=$target;backup=$backupFile;old_sha256=$before;new_sha256=$manifest.nro_sha256;firmware_changed=$false}
$state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'update.json') -Encoding UTF8
Write-Output 'USB Loader 0.6.9 installed and SHA-256 verified. Firmware, HOC and game mods were not changed.'
Write-Output "Previous app: $backupFile"
