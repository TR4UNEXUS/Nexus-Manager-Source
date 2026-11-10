#!/usr/bin/env python3
"""Run the production import orchestration and rollback core against faulting IO.

This does not model Switch services or crypto. Those boundaries are injected;
the real public import function decides when verification, journal activation,
registration, rollback, and completion are allowed.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/usb_import.cpp').read_text()
scan_source = (root / 'sphaira/source/nexus/usb_import_scan.cpp').read_text()

def function(marker, text=source):
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end]

template = (root / 'tests/usb_import_orchestration_tests.cpp').read_text()
template = template.replace('// PRODUCTION_IMPORT', function('Result ImportUsbLibrary('))
template = template.replace('// PRODUCTION_PLAN', function('UsbLibraryPlan PlanUsbLibrary(', scan_source).replace('core::', 'nexus::usb_import::'))
identity = function('Result SystemIdentity(')
fixture = template.replace('// PRODUCTION_IDENTITY', identity)
with tempfile.TemporaryDirectory(prefix='nexus-import-orchestration-') as tmp:
    path = Path(tmp)
    (path / 'tests.cpp').write_text(fixture)
    for opt in ('-O0', '-O2'):
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', opt,
                        '-I', str(root / 'sphaira/include'), str(path / 'tests.cpp'),
                        '-lcrypto', '-o', str(path / 'tests')], check=True)
        subprocess.run([str(path / 'tests')], check=True)
    # Reproduce the prior mandatory-EFS0 predicate in the same full import
    # fixture. This is a temporary negative-control binary, never production.
    fixed_predicate = '(config.magic==0x30534645 || empty_sysmmc)'
    assert fixed_predicate in identity
    old_identity = identity.replace(fixed_predicate, 'config.magic==0x30534645')
    old_identity = old_identity.replace('const bool empty_sysmmc=', '[[maybe_unused]] const bool empty_sysmmc=')
    legacy = template.replace('// PRODUCTION_IDENTITY', old_identity)
    legacy = legacy[:legacy.index('int main(){')] + '''int main(){
        Reset();UsbImportResult out;
        REQUIRE(Run(Scan(),out)==0x1759);
        REQUIRE(out.result==0x1759&&world.journal_writes==0&&world.live_writes==0);
        REQUIRE(world.calls["verify"]==0&&world.calls["publish.before"]==0);
        std::cout<<"Legacy mandatory-EFS0 negative control: zero sysMMC reproduces 0x1759 before mutation\\n";
    }
    '''
    (path / 'legacy.cpp').write_text(legacy)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O0',
                    '-I', str(root / 'sphaira/include'), str(path / 'legacy.cpp'),
                    '-lcrypto', '-o', str(path / 'legacy')], check=True)
    subprocess.run([str(path / 'legacy')], check=True)
