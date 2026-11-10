#!/usr/bin/env python3
"""Compile the production archive adapter against an in-memory fault-injection IO."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / 'sphaira/include/nexus/usb_ticket_store.hpp').read_text()
source = (root / 'sphaira/source/nexus/usb_ticket_store.cpp').read_text()
header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', header, flags=re.M)
source = re.sub(r'^#include "[^\n]*\n', '', source, flags=re.M)
fixture = (root / 'tests/usb_ticket_store_fixture.hpp').read_text()
cases = (root / 'tests/usb_ticket_store_tests.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-ticket-tests-') as tmp:
    path = Path(tmp)
    (path / 'tests.cpp').write_text(fixture + '\n' + header + '\n' + source + '\n' + cases)
    for opt in ('-O0', '-O2'):
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', opt,
                        str(path / 'tests.cpp'), '-o', str(path / 'tests')], check=True)
        subprocess.run([str(path / 'tests')], check=True)
