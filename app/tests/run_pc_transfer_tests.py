"""Exercise production FTP bridge, staging and USB preflight against isolated disks."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/transfer.cpp').read_text()
fixture=(root/'tests/pc_transfer_fixture.cpp').read_text()
area=source[source.index('constexpr Result Bad='):source.index('class Proxy final:')]
bridge=source[source.index('const char* FtpName('):source.index('FtpVfs bridge_vfs=')]
preflight=source[source.index('        u64 usbPin{};'):source.index('        R_TRY(StartupStep("pc-games-mount"')]
fixture=fixture.replace('// AREA_PRODUCTION',area).replace('// BRIDGE_PRODUCTION',bridge).replace('// USB_PREFLIGHT_PRODUCTION',preflight)
fixture=fixture.replace('// INSTALL_QUEUE_PRODUCTION',source[source.index('bool TakeInstall('):source.index('void Stop()')])
with tempfile.TemporaryDirectory(prefix='nexus-pc-transfer-') as directory:
    work=Path(directory);cpp=work/'test.cpp';cpp.write_text(fixture)
    for opt in ('-O0','-O2'):
        binary=work/('test'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-Wno-format-truncation',
            '-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(root/'sphaira/include'),str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
