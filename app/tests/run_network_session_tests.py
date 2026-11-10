"""Exercise real Session ownership and libnx socket singleton semantics on host.

Only the Switch IPC and device registry calls are mocked. This does not simulate
the NAS server; it specifically tests teardown/reopen and partial-start failures.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
network = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
libnx = (root.parent / 'dependencies/libnx-source/nx/source/runtime/devices/socket.c').read_text()
bsd = (root.parent / 'dependencies/libnx-source/nx/source/services/bsd.c').read_text()
session = network[network.index('struct Session {'):network.index('using SessionPtr=')]
socket = libnx[libnx.index('Result socketInitialize('):libnx.index('Result socketGetLastResult(')]
defaults = libnx[libnx.index('static const SocketInitConfig g_defaultSocketInitConfig'):libnx.index('static u32 socketSelectVersion(')]
pool = bsd[bsd.index('NX_CONSTEXPR size_t _bsdGetTransferMemSizeForConfig('):bsd.index('NX_CONSTEXPR BsdSelectTimeval')].replace('NX_CONSTEXPR ', '')
fixture = (root / 'tests/network_session_fixture.cpp').read_text()
fixture = fixture.replace('// LIBNX_DEFAULT_CONFIG_PRODUCTION', defaults)
fixture = fixture.replace('// LIBNX_POOL_SIZE_PRODUCTION', pool)
fixture = fixture.replace('// LIBNX_SOCKET_PRODUCTION', socket)
fixture = fixture.replace('// NETWORK_SESSION_PRODUCTION', session)
with tempfile.TemporaryDirectory(prefix='nexus-network-session-') as directory:
    work = Path(directory)
    source = work / 'test.cpp'
    source.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('test' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-g', '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(root / 'sphaira/include'), str(source),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
