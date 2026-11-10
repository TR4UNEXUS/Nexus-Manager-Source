#!/usr/bin/env python3
"""Exercise the production import journal and NS intermediate-state rules."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/usb_import.cpp').read_text()
keys = source[source.index('core::Key Key('):source.index('std::string Label(')]
records = source[source.index('bool SameRecord('):source.index('struct Disk {')]
intermediate = source[source.index('    bool HomeIntermediate('):source.index('    bool IndexesRestore(')]
system_identity = source[source.index('Result SystemIdentity('):source.index('struct Path {')]
activate = source[source.index('bool Activate(Disk&'):source.index('void SaveReport(')].replace('bool Activate(Disk&', 'bool Activate(ActivationDisk&')
def method(name):
    start = source.index('    bool ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[start:end]
tests = (root / 'tests/usb_import_tests.cpp').read_text()
tests = tests.replace('// KEY_RECORD_PRODUCTION', keys + '\n' + records)
tests = tests.replace('// SYSTEM_IDENTITY_PRODUCTION', system_identity)
tests = tests.replace('// ACTIVATE_PRODUCTION', activate)
tests = tests.replace('// HOME_INTERMEDIATE_PRODUCTION', intermediate)
tests = tests.replace('// HOME_WRITE_PRODUCTION', method('HomeWrite'))
tests = tests.replace('// FINISH_PRODUCTION', method('Finish'))
with tempfile.TemporaryDirectory(prefix='nexus-import-tests-') as tmp:
    path = Path(tmp)
    (path / 'tests.cpp').write_text(tests)
    for opt in ('-O0', '-O2'):
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', opt,
                        '-I', str(root / 'sphaira/include'), str(path / 'tests.cpp'),
                        '-o', str(path / 'tests')], check=True)
        subprocess.run([str(path / 'tests')], check=True)
