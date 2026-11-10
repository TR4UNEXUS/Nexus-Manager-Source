"""Run production Save/Load with the shipped minIni and real temporary files."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
network = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
constant = network[network.index('constexpr const char* ConfigPath='):network.index('struct Session {')]
production = network[network.index('std::vector<Connection> Load()'):network.index('struct Hash {')]
fixture = (root / 'tests/network_persistence_fixture.cpp').read_text()
fixture = fixture.replace('// CONFIG_PATH_PRODUCTION', constant)
fixture = fixture.replace('// NETWORK_PERSISTENCE_PRODUCTION', production)
with tempfile.TemporaryDirectory(prefix='nexus-network-persistence-') as directory:
    work = Path(directory)
    generated = work / 'test.cpp'
    generated.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        ini = work / ('minini' + optimization + '.o')
        binary = work / ('test' + optimization)
        subprocess.run(['gcc', '-std=c11', optimization, '-D_POSIX_C_SOURCE=200809L', '-include', 'strings.h',
                        '-Dini_puts=real_ini_puts', '-Drename=test_rename',
                        '-DMININI_USE_NX=0', '-DMININI_USE_STDIO=1',
                        '-I', str(root / 'vendor/minini/include'), '-c',
                        str(root / 'vendor/minini/source/minIni.c'), '-o', str(ini)], check=True)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-I', str(root / 'sphaira/include'),
                        '-I', str(root / 'vendor/minini/include'), str(generated), str(ini),
                        '-o', str(binary)], check=True)
        run = work / optimization
        run.mkdir()
        subprocess.run([str(binary)], cwd=run, check=True)
