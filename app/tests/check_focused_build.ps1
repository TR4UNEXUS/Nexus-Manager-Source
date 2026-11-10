$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$menu=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/ui/menus/main_menu.cpp') -Raw
$app=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/app.cpp') -Raw
$main=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/main.cpp') -Raw
$cmake=Get-Content -LiteralPath (Join-Path $root 'sphaira/CMakeLists.txt') -Raw
if($cmake -notmatch '(?s)set_property\(SOURCE source/owo.cpp APPEND PROPERTY OBJECT_DEPENDS\s+"\$\{CMAKE_BINARY_DIR\}/hbl/exefs/main"\s+"\$\{CMAKE_BINARY_DIR\}/hbl/exefs/main.npdm"') {throw 'Embedded forwarder incremental rebuild dependencies missing'}
$manager=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/nexus/manager.cpp') -Raw
$nexusMenu=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/ui/menus/nexus_menu.cpp') -Raw
$base=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/ui/menus/menu_base.cpp') -Raw
$updater=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/nexus/update.cpp') -Raw
$errorBox=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/ui/error_box.cpp') -Raw
if($menu -notmatch 'make_unique<nexusmenu::Menu>' -or $menu -notmatch 'return \{\};') {throw 'Not a fixed single workspace'}
if($menu -match 'Display.*Options|Appstore|Homebrew|FileBrowser|CheckForUpdates|CreateLeft|CreateRight') {throw 'Extra menu path remains'}
if($cmake -match 'source/ui/menus/(appstore|homebrew|filebrowser|save_menu|storage_menu|themezer|ghdl|irs_menu|game_menu)\.cpp') {throw 'Extra menu compiled'}
if($main -match 'socketInitialize|nifmInitialize|log_nxlink_init') {throw 'Unneeded network startup remains'}
if($app -match 'curl::Init\(|audio::Init\(|devoptab::MountVfsAll\(') {throw 'Background extras still started'}
if($manager -notmatch 'nexus::ValidMove\(source,target\)' -or $manager -notmatch 'c.DeleteRegistered\(src.generation,path,size\)') {throw 'Bidirectional/physical-delete guards missing'}
if($manager -notmatch 'source==Target::Internal && !nexus::ShareMetadata') {throw 'Shared SD metadata not protected'}
if($nexusMenu -match 'cover flow|depth\*side|216.f-depth') {throw 'Cover flow remains'}
if($nexusMenu -notmatch 'view::PageStart\(selected\)' -or $nexusMenu -notmatch 'view::Vertical\(selected,Count\(\),true\)') {throw 'Paged library navigation missing'}
# The four main actions use dedicated library/install/file-manager/tool icons. Cleanup
# remains a reviewed tool action; it is no longer a mandatory dashboard tile.
if($nexusMenu -notmatch 'view::PaintIcon\(painter,icon\)' -or $nexusMenu -notmatch 'view::MenuIcon::HomeLibrary,view::MenuIcon::HomeInstall,view::MenuIcon::FileManager,view::MenuIcon::More') {throw 'Main action icons missing'}
if($base -notmatch 'class StoragePoller' -or $base -notmatch 'c.GetSpace\(status.generation,space\)' -or $base -notmatch 'StorageCard\(vg,1020,"USB"') {throw 'Shared USB telemetry widget missing'}
if($updater -notmatch 'CURLOPT_SSL_VERIFYPEER,1L' -or $updater -notmatch 'CURLOPT_SSL_VERIFYHOST,2L' -or $updater -notmatch 'Hex\(digest,32\)==release.sha256') {throw 'Updater verification missing'}
if($errorBox -match 'github.com/ITotalJustice|github.com/NaGaa95|return "Sphaira"') {throw 'Upstream error-screen branding remains'}
if($errorBox -notmatch 'm_message_scroll.Draw' -or $errorBox -notmatch 'Button::UP' -or $errorBox -notmatch 'Button::DOWN' -or $errorBox -notmatch 'Button::B' -or $errorBox -notmatch 'touch->in_range') {throw 'Scrollable/dismissible USB Loader error dialog missing'}
foreach($name in @('error_box','option_box','progress_box','sidebar','popup_list','notification')) {
  $modal=Get-Content -LiteralPath (Join-Path $root "sphaira/source/ui/$name.cpp") -Raw
  if($modal -notmatch 'nexusdialog::Panel') {throw "Unstyled modal: $name"}
}
if($manager -match 'memcmp\(&r.key,&m.key|memcmp\(&m.key,&key') {throw 'Padding-sensitive metadata key comparison remains'}
if($manager -notmatch 'remove-preflight-paths' -or $manager -notmatch 'nexus::Log\(event,result,stage_value\)' -or $manager -notmatch 'nexus::Log\("remove-count-M4",result,report.removed\)' -or $manager -notmatch 'NcmContentInstallType_Unknown' -or $manager -notmatch 'nexus::installed_move::VisitReferences<NcmContentInfo>') {throw 'Removal diagnostics/reference guards missing'}
$sdSize=[regex]::Match($app,'(?s)Result App::GetSdSize\(.*?(?=Result App::GetEmmcSize)').Value
if($sdSize -notmatch 'QueryPhysicalSd\(space\)' -or $sdSize -match 'FsNativeContentStorage') {throw 'Widget must query physical SD, never routed content FS'}
$client=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/nexus/nx_Client.cpp') -Raw
$physical=[regex]::Match($client,'(?s)Result QueryPhysicalSd\(.*?(?=Result Destination::Open)').Value
if($physical -notmatch 'fsOpenSdCardFileSystem' -or $physical -notmatch 'char root\[FS_MAX_PATH\]="/"' -or $physical -notmatch 'fsFsGetFreeSpace\(&sd,root' -or $physical -notmatch 'fsFsGetTotalSpace\(&sd,root' -or $physical -match 'ncmContentStorageGet|c.GetSpace' -or $physical -notmatch 'NEXUS_VSD_ACTIVE\|NEXUS_VSD_ARMED') {throw 'Physical SD root isolation check missing'}
if($nexusMenu -notmatch 'Page::Homebrew' -or $nexusMenu -notmatch 'void Menu::RunProbe' -or $nexusMenu -notmatch 'NEXUS_VSD_CAP_USB_ROOT' -or $nexusMenu -notmatch 'WHOLE USB drive') {throw 'Homebrew section or USB-root consent/capability check missing'}
$homebrew=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/nexus/homebrew.cpp') -Raw
if($homebrew -notmatch '#embed <VsdProbe.nro>' -or $homebrew -notmatch 'randomGet' -or $homebrew -notmatch 'client.CreateDirectory\(generation,root\)' -or $homebrew -notmatch 'memcmp\(check.data\(\),Probe\+off,count\)' -or $homebrew -match 'Delete|ResizeFile|system\(') {throw 'Bundled probe ownership/readback guards missing'}
$yati=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/yati/yati.cpp') -Raw
if($client -notmatch 's.generation!=required_generation' -or $yati -notmatch 'override.nexus_destination_generation' -or $yati -notmatch 'usb_generation=destination->Generation\(\)') {throw 'Batch destination generation pin missing'}
if($manager -notmatch 'batch::Run\(' -or $manager -notmatch 'c.Check\(source_generation\)' -or $manager -notmatch 'c.Check\(destination_generation\)' -or $manager -notmatch 'info.size==e.size' -or $manager -notmatch 'batch-last.log') {throw 'Batch preflight, generation checks or report missing'}
if($nexusMenu -notmatch 'void Menu::TogglePackage' -or $nexusMenu -notmatch 'void Menu::SelectAllPackages' -or $nexusMenu -notmatch 'void Menu::ChooseBatchDestination' -or $nexusMenu -notmatch 'row.state==nexus::batch::State::Installed\)packages.Remove') {throw 'Batch UI/selection result handling missing'}
Write-Output 'Focused-build source contracts passed (not a hardware or full transfer simulation).'
$embedded=Get-Content -LiteralPath (Join-Path $root 'hbl/source/main.c') -Raw
$owo=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/owo.cpp') -Raw
if($embedded.IndexOf('nexusVsdBeforeLoad(g_procHandle') -gt $embedded.IndexOf('fsOpenSdCardFileSystem(&fs)') -or $embedded -match 'fsFileRead\(&f, 0, start, g_heapSize') {throw 'Loader handoff/read-size contract failed'}
if($embedded -notmatch 'memcmp\(hash,initial_hash' -or $embedded -notmatch 'nexusUsbPath') {throw 'Forwarder target binding missing'}
if($owo -notmatch 'exclusive_forwarder=usb_forwarder\|\|config.port_shortcut' -or $owo -notmatch 'exclusive_forwarder\?0:nsDeleteApplicationCompletely' -or $owo -notmatch 'if\(!exclusive_forwarder\)nsDeleteApplicationEntity' -or $owo -notmatch 'application_id!=tid' -or $owo -notmatch 'content_meta_count = 0;' -or $owo -notmatch 'ncmU64ToContentInfoSize\(entry.nca_entry.data.size\(') {throw 'Safe forwarder registration contract failed'}
if($nexusMenu -notmatch 'Homebrew/USB' -or $nexusMenu -notmatch 'game.forwarder_path.empty' -or $nexusMenu -notmatch '(?s)if\(!game.homebrew_path.empty\(\)\)\{\s*QueueIcon\(game\);continue;\s*\}else\{\s*if\(title_requested.insert\(game.id\).second\)title::PushAsync\(game.id\);' -or $homebrew -notmatch 'ScanHomebrew' -or $homebrew -notmatch 'c.Check\(status.generation\)') {throw 'USB library separation/generation checks missing'}
Write-Output 'New HOME launcher / USB library / forwarder source contracts passed.'
$export=[regex]::Match($owo,'(?s)Result export_forwarder_debug\(.*?(?=auto install_forwader_internal)').Value
if($export -notmatch 'QueryPhysicalSd\(space\)' -or $export -notmatch 'fsFsCreateFile\(&sd,path,package.size\(\),0\)' -or $export -notmatch 'FsWriteOption_Flush' -or $export -notmatch 'read==package.size\(\) && verify==package' -or $export -match 'DeleteFile|DeleteDirectory') {throw 'Forwarder diagnostic export safety contract failed'}
$exportCall=$owo.IndexOf('export_forwarder_debug(tid,nca_entries)')
if($exportCall -lt 0 -or $exportCall -ge $owo.IndexOf('// write ncas') -or $owo.Substring($exportCall,200) -notmatch 'R_TRY\(rc\)') {throw 'Diagnostic export must succeed before NCM writes'}
if($embedded.IndexOf('nexusTraceOpen();') -ge $embedded.IndexOf('loadNro();', $embedded.IndexOf('int main(')) -or $embedded -notmatch 'nexusTraceClose\(\);') {throw 'Forwarder startup trace lifetime contract failed'}
Write-Output 'Forwarder diagnostic export source guards passed (not a hardware simulation).'
$startup=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/nexus/startup_trace.cpp') -Raw
$titleInfo=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/title_info.cpp') -Raw
$transport=Get-Content -LiteralPath (Join-Path $root 'hbl/source/nexus_vsd.c') -Raw
if($startup -match 'smGetService|serviceDispatch|DeleteFile|DeleteDirectory|svcSleepThread|mutexLock\(') {throw 'Startup logger must not call USB, reset, delete, sleep or wait on logger contention'}
$traceRing=Get-Content -LiteralPath (Join-Path $root 'sphaira/include/nexus/trace_ring.hpp') -Raw
if($startup -notmatch 'mutexTryLock' -or $startup -notmatch 'skipped.exchange' -or $startup -notmatch 'FsWriteOption_Flush' -or $startup -notmatch 'TraceRing runtime_ring\{0, 819\}, exit_ring\{0, 102\}' -or $startup -notmatch 'std::min\(count,ring.capacity-ring.slot\)' -or $traceRing -notmatch 'RecordBytes = 320' -or $traceRing -notmatch 'slot = \(slot \+ 1\) % capacity') {throw 'Startup trace bounds/concurrency/flush safeguards missing'}
if($main.IndexOf('nexus::startup::Open();') -gt $main.IndexOf('sphaira::App::SetBoostMode(true)') -or $main -notmatch 'main-menu-constructor' -or $main -notmatch 'app-destructor') {throw 'Early app startup boundaries missing'}
foreach($stage in @('ncm-open-meta-database','ncm-open-content-storage','title-ns-initialize','title-worker-start')) {if($titleInfo -notmatch $stage){throw "Missing title startup stage $stage"}}
foreach($stage in @('usb-service-open','usb-status-query','usb-vsd-query','physical-sd-open','usb-space-query')) {if($client -notmatch $stage){throw "Missing client startup stage $stage"}}
if($embedded -notmatch 'app-init-fs-ready' -or $transport -notmatch 'vsd-service-open-begin' -or $transport -notmatch 'vsd-query-begin') {throw 'Early embedded HOME diagnostic stages missing'}
Write-Output 'Paired startup trace source contracts passed (not a hardware simulation).'
$hblTrace=Get-Content -LiteralPath (Join-Path $root 'hbl/source/nexus_trace.c') -Raw
foreach($logger in @($startup,$hblTrace)) {
  if($logger -notmatch 'char path\[FS_MAX_PATH\]' -or $logger -notmatch 'directories\[\]\[FS_MAX_PATH\]' -or $logger -match 'fsFsCreateDirectory\([^,]+,\s*"') {throw 'Undersized logger IPC path buffer'}
}
if($export -notmatch 'char path\[FS_MAX_PATH\]' -or $export -notmatch 'char dir\[FS_MAX_PATH\]') {throw 'Undersized diagnostic export IPC path buffer'}
if($main -notmatch 'startup::FailureInfo' -or $main -notmatch 'Startup log unavailable') {throw 'Missing visible logger failure status'}
Write-Output 'Full-size filesystem paths and visible logger-failure contracts passed.'
$progress=Get-Content -LiteralPath (Join-Path $root 'sphaira/source/ui/progress_box.cpp') -Raw
$guard=Get-Content -LiteralPath (Join-Path $root 'common/watchdog.c') -Raw
if($progress -match 'RequestExit\(\);\s*SetPop\(\)' -or $progress -notmatch 'if \(m_safety.CanDismiss\(\)\)' -or $progress -notmatch 'd->pbox->WorkerFinished') {throw 'Cancellation must never dismiss a live worker'}
if($progress -notmatch 'Operation::MayWrite' -or $progress -notmatch 'nexusWatchdogProtect\(\)' -or $progress -notmatch 'nexusWatchdogUnprotect\(\)' -or $progress -notmatch 'm_safety.CanClose') {throw 'Read-only exit/write protection missing'}
if($guard -match 'fsFs|fsFile|smGet|serviceDispatch|Reset|Unmount|svcTerminateProcess' -or $guard -notmatch 'svcExitProcess' -or $guard -notmatch 'waitSingleHandle') {throw 'Watchdog must only close its own process, without USB/FS calls'}
$ctor=[regex]::Match($nexusMenu,'(?s)Menu::Menu\(.*?(?=Menu::~Menu)').Value
$update=[regex]::Match($nexusMenu,'(?s)void Menu::Update\(.*?(?=void Menu::Draw)').Value
if($ctor -match 'title::Init' -or $update -match 'ReadHomebrewIcon|VirtualSdStatus|ArmVirtualSd' -or $nexusMenu -notmatch 'ReadIconWorker') {throw 'Blocking USB/title reads returned to main UI thread'}
if($embedded.IndexOf('nexusWatchdogStop();') -ge $embedded.IndexOf('nroEntrypointTrampoline(&entries') -or $main -notmatch 'nexusWatchdogDisarm\(\)' -or $main -notmatch 'nexusWatchdogStop\(\)') {throw 'Watchdog runtime handoff lifetime contract failed'}
Write-Output 'Busy/cancel/worker lifetime and no-reset watchdog source contracts passed.'

# 0.6.7 startup sequence shared by every generated HOME forwarder. Initial
# filesystem/logger setup must precede helper-thread creation, and a diagnostic
# helper failure must not silently exit the process. Runtime tests supplement
# these source guards; this does not assert all homebrew is hardware-compatible.
$init=$embedded.Substring($embedded.IndexOf('void __appInit(void)'))
$monitor=[regex]::Match($embedded,'(?s)static void startLaunchMonitor\(void\)\s*\{.*?\n\}').Value
if(!$monitor -or $monitor -match 'svcExitProcess|fatal|diagAbort' -or
   $monitor -notmatch 'if \(g_launchMonitorUnavailable\) return;' -or
   $monitor -notmatch 'if \(R_FAILED\(rc\)\) g_launchMonitorUnavailable=true;') {throw 'Forwarder monitor failure must remain nonfatal and remembered'}
if($init.IndexOf('startLaunchMonitor();') -le $init.IndexOf('nexusTrace("app-init-fs-ready"') -or
   $init.IndexOf('startLaunchMonitor();') -ge $init.IndexOf('smExit();') -or
   $init -match 'nexusWatchdogStart\(' -or
   ([regex]::Matches($embedded,'nexusWatchdogStart\(')).Count -ne 1) {throw 'Forwarder monitor reverted to pre-service initialization'}
if(([regex]::Matches($embedded,'startLaunchMonitor\(\);')).Count -ne 2 -or
   $monitor -notmatch 'watchdog-start-begin' -or $monitor -notmatch 'watchdog-start-end') {throw 'Forwarder startup/chainload diagnostics missing'}
if($embedded -match 'nfsmw|0565106[bB]0453[dD]000') {throw 'General forwarder must not hardcode the tested NFS shortcut'}
Write-Output 'General forwarder startup/monitor source contracts passed.'
