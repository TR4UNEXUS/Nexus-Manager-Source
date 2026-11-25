#!/usr/bin/env python3
"""Run production updater request/worker code with transport and libnx faults."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/update.cpp').read_text()
backend = source[source.index('namespace nexus::updates {'):source.index('Result FetchHatsData(')]
fixture = (root / 'tests/startup_update_fixture.cpp').read_text()
fixture = fixture.replace('// UPDATE_BACKEND_PRODUCTION', backend + '\n}')
cases = ('new-nro', 'new-nsp', 'current', 'invalid', 'offline', 'owned-sockets',
         'http-error', 'oversized', 'redirect', 'cancel', 'dns-cancel', 'dns-timeout',
         'thread-create', 'thread-start', 'deadline-create', 'deadline-start', 'completed-stop')
with tempfile.TemporaryDirectory(prefix='nexus-startup-update-') as directory:
    work = Path(directory)
    test = work / 'test.cpp'
    test.write_text(fixture)
    yyjson = work / 'yyjson.o'
    subprocess.run(['gcc', '-c', '-O2', str(root / 'vendor/yyjson/src/yyjson.c'), '-o', str(yyjson)], check=True)
    for optimization in ('-O0', '-O2'):
        binary = work / ('test' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-g', '-UNDEBUG', '-pthread',
                        '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-unused-function',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(root / 'sphaira/include'), '-I', str(root / 'vendor/yyjson/src'),
                        str(test), str(root / 'sphaira/source/nexus/release.cpp'), str(yyjson),
                        '-o', str(binary)], check=True)
        for case in cases:
            subprocess.run([str(binary), case], check=True, timeout=10)
print('Production startup updater tests passed at -O0 and -O2.')
