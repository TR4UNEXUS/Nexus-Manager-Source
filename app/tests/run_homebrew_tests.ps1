param([string]$Python='python',[string]$Libnx=$env:LIBNX)
$ErrorActionPreference='Stop'
$app=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) -replace '\\','/'
if(!$Libnx){$Libnx=Join-Path (Split-Path -Parent $app) 'fw23-build-deps/libnx'}
$out=Join-Path $app 'build/homebrew-tests'
[IO.Directory]::CreateDirectory($out) | Out-Null
& $Python "$app/tests/hbl_startup_tests.py"
if($LASTEXITCODE){throw 'HBL startup monitor helper tests failed'}
& C:/devkitPro/msys2/usr/bin/gcc.exe -std=gnu11 -O1 -Wall -Wextra -Werror "-I$app/tests/hbl_mock" "-I$app/sphaira/include" "-I$app/hbl/source" "$app/tests/hbl_usb_read_tests.c" "$app/hbl/source/nexus_vsd.c" -o "$out/hbl_usb_read_tests.exe"
if($LASTEXITCODE){throw 'USB reader test compile failed'}
& "$out/hbl_usb_read_tests.exe"
if($LASTEXITCODE){throw 'USB reader tests failed'}
& $Python "$app/tests/bootstrap_registration_tests.py"
if($LASTEXITCODE){throw 'Lazy bootstrap registration tests failed'}
& C:/devkitPro/msys2/usr/bin/gcc.exe -std=c11 -O1 -Wall -Wextra -Werror "-I$app/tests/watchdog_mock" "-I$app/sphaira/include" "$app/tests/watchdog_tests.c" "$app/common/watchdog.c" -o "$out/watchdog_tests.exe"
if($LASTEXITCODE){throw 'Watchdog test compile failed'}
foreach($mode in @('expiry','disarm','protected','rearm','create-fail','start-fail','stop','stop-fail','manual','manual-protected')){
  & "$out/watchdog_tests.exe" $mode
  if($LASTEXITCODE){throw "Watchdog tests failed: $mode"}
}
& C:/devkitPro/msys2/usr/bin/g++.exe -std=c++20 -O1 -Wall -Wextra -Werror "-I$app/sphaira/include" "$app/tests/progress_safety_tests.cpp" -o "$out/progress_safety_tests.exe"
if($LASTEXITCODE){throw 'Progress safety test compile failed'}
& "$out/progress_safety_tests.exe"
if($LASTEXITCODE){throw 'Progress safety tests failed'}
& C:/devkitPro/msys2/usr/bin/gcc.exe -std=c11 -O1 -DNEXUS_PATH_CHECKS "-I$app/tests/hbl_mock" "$app/tests/fs_path_bounds_tests.c" -o "$out/fs_path_bounds_tests.exe"
if($LASTEXITCODE){throw 'Fixed-size FS path test compile failed'}
foreach($mode in @('literal','short','full')) {
  & "$out/fs_path_bounds_tests.exe" $mode
  $expected=if($mode -eq 'full'){0}else{91}
  if($LASTEXITCODE -ne $expected){throw "Fixed-size FS path negative control failed: $mode"}
}
Write-Output 'FS path regression harness rejects string literals/160-byte paths and accepts FS_MAX_PATH arrays.'
& C:/devkitPro/msys2/usr/bin/g++.exe -std=c++20 -O1 -Wall -Wextra -Werror "-I$app/sphaira/include" "$app/tests/library_tests.cpp" -o "$out/library_tests.exe"
if($LASTEXITCODE){throw 'Library test compile failed'}
& "$out/library_tests.exe"
if($LASTEXITCODE){throw 'Library tests failed'}
& C:/devkitPro/msys2/usr/bin/gcc.exe -std=c11 -O1 "-I$app/tests/hbl_mock" "-I$app/sphaira/include" "-I$app/hbl/source" "$app/tests/hbl_transport_tests.c" "$app/hbl/source/nexus_vsd.c" -o "$out/hbl_transport_tests.exe"
if($LASTEXITCODE){throw 'Loader transport test compile failed'}
& "$out/hbl_transport_tests.exe"
if($LASTEXITCODE){throw 'Loader transport tests failed'}
& $Python "$app/tests/forwarder_romfs_tests.py" --libnx $Libnx
if($LASTEXITCODE){throw 'Production forwarder RomFS round-trip tests failed'}
& $Python "$app/tests/forwarder_npdm_tests.py"
if($LASTEXITCODE){throw 'Production forwarder NPDM tests failed'}
& C:/devkitPro/msys2/usr/bin/gcc.exe -std=c11 -O1 -DNEXUS_PATH_CHECKS "-I$app/tests/hbl_mock" "-I$app/hbl/source" "$app/tests/hbl_trace_tests.c" "$app/hbl/source/nexus_trace.c" -o "$out/hbl_trace_tests.exe"
if($LASTEXITCODE){throw 'Loader trace test compile failed'}
foreach($mode in @('normal','open-fail','create-fail','write-fail','timed','unresolved')){
  & "$out/hbl_trace_tests.exe" $mode
  if($LASTEXITCODE){throw "Loader trace tests failed: $mode"}
}
& C:/devkitPro/msys2/usr/bin/g++.exe -std=c++20 -O1 -Wall -Wextra -Werror -DNEXUS_PATH_CHECKS "-I$app/tests/startup_mock" "-I$app/sphaira/include" "$app/tests/startup_trace_tests.cpp" "$app/sphaira/source/nexus/startup_trace.cpp" -o "$out/startup_trace_tests.exe"
if($LASTEXITCODE){throw 'App startup trace test compile failed'}
foreach($mode in @('normal','open-fail','create-fail','file-open-fail','write-fail','exit-write-fail')){
  & "$out/startup_trace_tests.exe" $mode
  if($LASTEXITCODE){throw "App startup trace tests failed: $mode"}
}
