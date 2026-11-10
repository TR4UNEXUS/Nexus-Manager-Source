"""Run discovery protocol, local subnet bounds and scripted socket fault tests."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-smb-discovery-') as directory:
    work=Path(directory)
    production=(root/'sphaira/source/nexus/smb_discovery.cpp').read_text()
    production=production[production.index('namespace sphaira::nexusapp {'):]
    production=production.replace('using Clock=std::chrono::steady_clock;','using Clock=FakeClock;')
    fixture=(root/'tests/smb_discovery_socket_fixture.cpp').read_text().replace('// DISCOVERY_PRODUCTION',production)
    extracted=work/'socket_tests.cpp';extracted.write_text(fixture)
    sanitizer='address,undefined' if '--address-sanitizer' in sys.argv else 'undefined'
    for index,source in enumerate((root/'tests/smb_discovery_tests.cpp',extracted)):
        binary=work/('tests'+str(index))
        subprocess.run(['g++','-std=c++20','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror',
            '-Wno-misleading-indentation','-fsanitize='+sanitizer,'-fno-omit-frame-pointer',
            '-I',str(root/'sphaira/include'),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
