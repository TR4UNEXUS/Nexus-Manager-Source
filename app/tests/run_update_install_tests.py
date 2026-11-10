#!/usr/bin/env python3
"""Run actual update.cpp Install orchestration with platform-boundary faults."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
production = (root / 'sphaira/source/nexus/update.cpp').read_text()


def function(signature):
    start = production.index(signature)
    opening = production.index('{', start)
    depth = 0
    for position in range(opening, len(production)):
        if production[position] == '{':
            depth += 1
        elif production[position] == '}':
            depth -= 1
            if depth == 0:
                return production[start:position + 1]
    raise AssertionError('Unclosed production function: ' + signature)


fixture = (root / 'tests/update_install_fixture.cpp').read_text()
for marker, signature in (
    ('// INSTALL_HEX_PRODUCTION', 'std::string Hex('),
    ('// INSTALL_FUNCTION_PRODUCTION', 'Result Install(ProgressBox*'),
):
    assert fixture.count(marker) == 1
    fixture = fixture.replace(marker, function(signature))

cases = (
    'sd', 'internal', 'existing-dir', 'applet', 'wrong-kind', 'current-version',
    'wrong-owner', 'wrong-applet', 'wrong-program', 'program-query', 'storage-query',
    'storage-none', 'storage-system', 'storage-gamecard', 'old-firmware', 'network', 'fetch',
    'download-corrupt', 'wrong-size', 'invalid-payload', 'mkdir', 'folder-file',
    'stage-open', 'existing-part', 'short-write', 'flush', 'sync', 'close',
    'read-open', 'read-error', 'read-corrupt', 'sd-device', 'stage-commit',
    'cancel', 'existing-final', 'rename', 'publish-commit', 'sd-open', 'installer',
    'nro', 'nro-path', 'nro-missing', 'nro-backup', 'nro-rename', 'nro-backup-commit',
)
with tempfile.TemporaryDirectory(prefix='nexus-update-install-') as directory:
    work = Path(directory)
    source = work / 'production_install_tests.cpp'
    source.write_text(fixture)
    yyjson = work / 'yyjson.o'
    subprocess.run(['gcc', '-c', '-O2', str(root / 'vendor/yyjson/src/yyjson.c'),
                    '-o', str(yyjson)], check=True)
    for optimization in ('-O0', '-O2'):
        binary = work / ('update_install' + optimization)
        subprocess.run([
            'g++', '-std=c++20', optimization, '-g', '-UNDEBUG', '-Wall', '-Wextra',
            '-Werror', '-Wno-misleading-indentation', '-Wno-deprecated-declarations',
            '-fsanitize=undefined', '-fno-sanitize-recover=all',
            '-I', str(root / 'sphaira/include'), '-I', str(root / 'vendor/yyjson/src'),
            str(source), str(root / 'sphaira/source/nexus/release.cpp'), str(yyjson),
            '-lcrypto', '-o', str(binary),
        ], check=True)
        for case in cases:
            sandbox = work / (case + optimization)
            sandbox.mkdir()
            subprocess.run([str(binary), case], cwd=sandbox, check=True, timeout=10)
print(f'Production Install tests passed: {len(cases)} cases at -O0 and -O2 with UBSan.')
