param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$appRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outRoot) | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Drawing
$nroPath = Join-Path $appRoot 'build/Nexus/switch/usbloader/USBLoader.nro'
$iconPath = Join-Path $appRoot 'assets/usb-loader-icon.jpg'
$homeNsp = Join-Path $outRoot 'USB-Loader-HOME-0.6.7.nsp'
if(-not (Test-Path -LiteralPath $homeNsp)) {throw 'Build/verify the HOME NSP first'}
$homeManifest=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($homeNsp,'.json')) -Raw | ConvertFrom-Json
if($homeManifest.nsp_sha256 -ne (Get-FileHash -LiteralPath $homeNsp).Hash.ToLowerInvariant() -or
   $homeManifest.nro_sha256 -ne (Get-FileHash -LiteralPath $nroPath).Hash.ToLowerInvariant()) {throw 'HOME NSP/NRO verification manifest mismatch'}
$nro = [IO.File]::ReadAllBytes($nroPath)
$ascii = [Text.Encoding]::ASCII
if($ascii.GetString($nro,0x10,4) -ne 'NRO0') { throw 'Invalid NRO magic' }
$aset = [BitConverter]::ToUInt32($nro,0x18)
if($ascii.GetString($nro,$aset,4) -ne 'ASET') { throw 'Missing NRO assets' }
$iconOffset = [BitConverter]::ToUInt64($nro,$aset+8)
$iconSize = [BitConverter]::ToUInt64($nro,$aset+16)
$nacpOffset = [BitConverter]::ToUInt64($nro,$aset+24)
$nacpSize = [BitConverter]::ToUInt64($nro,$aset+32)
$romfsOffset = [BitConverter]::ToUInt64($nro,$aset+40)
$romfsSize = [BitConverter]::ToUInt64($nro,$aset+48)
foreach($range in @(@($iconOffset,$iconSize),@($nacpOffset,$nacpSize),@($romfsOffset,$romfsSize))) {
  if($range[0] -lt 56 -or $range[1] -eq 0 -or $aset+$range[0]+$range[1] -gt $nro.Length) { throw 'Invalid NRO asset bounds' }
}
if($nacpSize -ne 0x4000) { throw 'Unexpected NACP size' }
$title = $ascii.GetString($nro,[int]($aset+$nacpOffset),512).Split([char]0)[0]
$version = $ascii.GetString($nro,[int]($aset+$nacpOffset+0x3060),16).Split([char]0)[0]
if($title -ne 'USB Loader' -or $version -ne '0.6.7') { throw "Wrong app identity: $title / $version" }
$embeddedIcon = New-Object byte[] $iconSize
[Array]::Copy($nro,[long]($aset+$iconOffset),$embeddedIcon,0,[long]$iconSize)
$sha = [Security.Cryptography.SHA256]::Create()
try {
  $actualIcon = [BitConverter]::ToString($sha.ComputeHash($embeddedIcon))
  $sourceIcon = [BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($iconPath)))
  if($actualIcon -ne $sourceIcon) { throw 'NRO contains a different icon' }
} finally { $sha.Dispose() }
$image = [Drawing.Image]::FromFile($iconPath)
try { if($image.Width -ne 256 -or $image.Height -ne 256) { throw 'Icon must be 256 x 256' } } finally { $image.Dispose() }

function AddFile($zip,[string]$path,[string]$entry) {
  if($entry -match '(^|/)\.\.?(/|$)|\\' -or [IO.Path]::IsPathRooted($entry)) { throw "Unsafe archive entry: $entry" }
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$path,$entry,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
function AddText($zip,[string]$entry,[string]$value) {
  $stream = $zip.CreateEntry($entry).Open()
  $writer = New-Object IO.StreamWriter($stream,(New-Object Text.UTF8Encoding($false)))
  try { $writer.Write($value) } finally { $writer.Dispose() }
}
function FilesInRepo([string]$root,[bool]$includeNew) {
  # An isolated source export has no .git directory. Enumerate source roots,
  # excluding build output and dependencies (packaged separately below).
  $paths = Get-ChildItem -LiteralPath $root -File -Recurse -Force | ForEach-Object {
    $rel=$_.FullName.Substring($root.TrimEnd('\','/').Length+1).Replace('\','/')
    if($rel -match '(^|/)(\.git|__pycache__|node_modules)(/|$)') { return }
    if($root -eq $appRoot -and $rel -match '^(build/|vendor/|\.github/|\.vscode/|build-.*\.log$|BUILD-MANIFEST.json$|DEPENDENCIES.json$)') { return }
    $rel
  }
  foreach($relative in ($paths | Sort-Object -Unique)) {
    # Do not publish inherited CI/funding/editor tasks as this app's automation.
    if($root -eq $appRoot -and $relative -match '^(\.github/|\.vscode/)') { continue }
    # Uninitialised, unused upstream dr_libs test-only submodule. No compiled
    # library/app code depends on it; its pin remains in .gitmodules/manifests.
    if($root.EndsWith('dr_libs-src') -and $relative -eq 'tests/external/miniaudio') { continue }
    $file = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $file.StartsWith($root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Source escaped root' }
    if(-not [IO.File]::Exists($file)) { throw "Missing source or unexported submodule: $relative" }
    if($relative -match '(^|/)(prod\.keys|title\.keys|keys\.dat|.*\.token)$|\.(nca|nsp|xci|utr)$') { throw "Unexpected private/game artifact: $relative" }
    [pscustomobject]@{Path=$file;Relative=$relative}
  }
}
$dependencies = @()
foreach($dep in (Get-Content -LiteralPath (Join-Path $appRoot 'DEPENDENCIES.json') -Raw | ConvertFrom-Json)) {
  if($dep.Name -notmatch '^[a-z0-9_]+$' -or $dep.Commit -notmatch '^[a-f0-9]{40}$') {throw 'Invalid dependency pin'}
  $depRoot=Join-Path $appRoot ('vendor/'+$dep.Name)
  if(-not (Test-Path -LiteralPath $depRoot -PathType Container)){throw 'Missing pinned dependency source'}
  $dependencies += [pscustomobject]@{Name=$dep.Name;Root=$depRoot;Commit=$dep.Commit;URL=$dep.URL}
}
$dependencyJson = ($dependencies | Select-Object Name,Commit,URL | ConvertTo-Json -Depth 4)
$manifest = [ordered]@{
  app='USB Loader'; version='0.6.7'; upstream='Sphaira 1.0.7';
  upstream_commit='99b6f08fd5c3effeb99df0130850403bd150fb33';
  hardware_tested=$false; focused_ui=$true; repository='https://github.com/TechRepairs4U/USB-Loader'; manual_updater=$true; storage_widgets=3; installed_view='4x2 icon grid'; move_directions=6; built_utc=(Get-Item -LiteralPath $nroPath).LastWriteTimeUtc.ToString('o');
  nro_sha256=(Get-FileHash -LiteralPath $nroPath -Algorithm SHA256).Hash.ToLowerInvariant();
  icon_sha256=(Get-FileHash -LiteralPath $iconPath -Algorithm SHA256).Hash.ToLowerInvariant();
  checks=@('Switch compilation','host six-direction routing/path/range/snapshot tests','grid navigation/icon geometry tests','focused menu source contracts','NRO asset bounds','embedded name/version','embedded 256x256 JPEG match');
  firmware_included=$false; cleanup_scope='explicitly reviewed NCM placeholders only'
  batch_install=$true; max_batch_packages=256; native_sd_widget=$true;
  homebrew_section=$true; home_cards=5; bundled_probe=$false; sd_capacity_source='raw SD root filesystem (not NCM)';
  home_launcher=$true; home_title_id='05005553424c0000'; home_nsp_sha256=$homeManifest.nsp_sha256;
  forwarder_creation='Internal Storage; no overwrite; USB executable hash bound';
  forwarder_romfs_fix='Preserve input order for 3+ files; production builder/reader round-trip verified';
  forwarder_address_space_bits=39; forwarder_cpu_core_mask='0xF (cores 0-3)';
  diagnostic_release=$true; forwarder_debug_path='SD:/switch/usbloader/forwarder-debug/';
  startup_debug_path='SD:/switch/usbloader/startup-debug/'; startup_debug_max_bytes=65536;
  fixed_size_fs_path_buffers=$true; visible_startup_log_error=$true;
  startup_shutdown_watchdog_seconds=30; busy_message_seconds=15;
  cancel_waits_for_worker=$true; write_operations_protected=$true;
  read_only_close_app=$true; lazy_title_init=$true; background_usb_icon_reads=$true;
  usb_reset_or_unmount=$false; kernel_deadlock_recovery=$false;
  application_switch_freeze_fixed=$false; home_news_stall_fixed=$false;
  startup_debug='Paired app/service/NCM stages with thread IDs and explicit skipped-event counter; matching HOME launcher required for pre-NRO tracing';
  forwarder_debug='Generated NSP export/readback plus bounded HBL startup traces; same startup code as successful NFS 0.6.7 test';
  forwarder_startup='Monitor starts after initial FS/logger setup; monitor failure logged, nonfatal, not retried; stopped before NRO entry';
  forwarder_hardware_evidence='NFS HOME shortcut 0.6.7 confirmed working by user and successful trace on 2026-09-30; not an all-homebrew compatibility guarantee';
  forwarder_monitor_before_initial_fs=$false; forwarder_monitor_failure_fatal=$false;
  existing_shortcuts_auto_updated=$false;
  startup_usb_registration='Deferred for fresh HOME app launch; lazy on explicit homebrew launch';
  forwarder_usb_read='Generation-pinned shared-owner reads, 1 MiB maximum, hash verification before VSD activation';
  sd_root_path_buffer_bytes=769;
  homebrew_library='USB /switch recursive, bounded; Homebrew/USB labels';
  probe_sha256=$null;
  batch_tests='host selection/order/limits, three targets, source preflight, failure, cancellation, reconnect'
  package_formats=@('NSP','NSZ','XCI','XCZ'); split_packages=$false; homebrew_usb_launch='Virtual SD protocol v1; working 0.5.1 firmware plus included HOME loader or matched hbl.nsp';
  container_tests='real PFS0/HFS0 parsers with synthetic fixtures: source/partition bounds, short reads, overflow, names, secure-only, prefixed XCI'
  excluded_test_submodule='dr_libs/tests/external/miniaudio @ 350784a9467a79d0fa65802132668e5afbcf3777 (not built)'
}
$runtimeZip = Join-Path $outRoot 'USB-Loader-0.6.7-20260930.zip'
$sourceZip = Join-Path $outRoot 'USB-Loader-0.6.7-Source-20260930.zip'
if((Test-Path -LiteralPath $runtimeZip) -or (Test-Path -LiteralPath $sourceZip)) { throw 'Refusing to overwrite an existing package' }
$zip = [IO.Compression.ZipFile]::Open($runtimeZip,[IO.Compression.ZipArchiveMode]::Create)
try {
  AddFile $zip $nroPath 'switch/usbloader/USBLoader.nro'
  AddFile $zip $homeNsp 'USB-Loader-HOME-0.6.7.nsp'
  AddFile $zip ([IO.Path]::ChangeExtension($homeNsp,'.json')) 'HOME-NSP-VERIFICATION.json'
  AddFile $zip (Join-Path $appRoot 'RELEASE-0.6.7.txt') 'READ-ME-FIRST.txt'
  AddFile $zip (Join-Path $appRoot 'LICENSE') 'LICENSE'
  AddFile $zip (Join-Path $appRoot 'ATTRIBUTION.md') 'ATTRIBUTION.md'
  AddText $zip 'BUILD-MANIFEST.json' ($manifest | ConvertTo-Json -Depth 5)
  AddText $zip 'DEPENDENCIES.json' $dependencyJson
  foreach($dep in $dependencies) {
    foreach($license in (Get-ChildItem -LiteralPath $dep.Root -File | Where-Object Name -Match '^(LICENSE|LICENCE|COPYING|COPYRIGHT)')) {
      AddFile $zip $license.FullName ('licenses/'+$dep.Name+'/'+$license.Name)
    }
  }
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::Open($sourceZip,[IO.Compression.ZipArchiveMode]::Create)
try {
  foreach($file in (FilesInRepo $appRoot $true)) { AddFile $zip $file.Path ('USB-Loader/'+$file.Relative) }
  foreach($dep in $dependencies) {
    foreach($file in (FilesInRepo $dep.Root $false)) { AddFile $zip $file.Path ('USB-Loader/vendor/'+$dep.Name+'/'+$file.Relative) }
  }
  AddText $zip 'USB-Loader/DEPENDENCIES.json' $dependencyJson
  AddText $zip 'USB-Loader/BUILD-MANIFEST.json' ($manifest | ConvertTo-Json -Depth 5)
} finally { $zip.Dispose() }
foreach($path in @($runtimeZip,$sourceZip)) {
  $zip = [IO.Compression.ZipFile]::OpenRead($path)
  try {
    if($zip.Entries.FullName | Where-Object {$_ -match '(^|/)(prod\.keys|title\.keys|keys\.dat|.*\.token)$|(^|/)\.git/|^USB-Loader/build/'}) { throw 'Unexpected package entry' }
    [pscustomobject]@{Path=$path;Files=$zip.Entries.Count;Bytes=(Get-Item -LiteralPath $path).Length;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
  } finally { $zip.Dispose() }
}
