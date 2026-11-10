#!/usr/bin/env python3
"""Exercise the actual preparation adapter with controlled ES, USB and SD IO."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / 'sphaira/include/nexus/usb_ticket_prepare.hpp').read_text()
source = (root / 'sphaira/source/nexus/usb_ticket_prepare.cpp').read_text()
header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', header, flags=re.M)
source = re.sub(r'^#include "[^\n]*\n', '', source, flags=re.M)
assert 'ImportTicket(' not in source and 'ncmContent' not in source and 'GetPersonalisedTicketAndCertificate' not in source
fixture = (root / 'tests/usb_ticket_prepare_fixture.hpp').read_text()
cases = (root / 'tests/usb_ticket_prepare_tests.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-prepare-tests-') as tmp:
    path = Path(tmp)
    (path / 'tests.cpp').write_text(fixture + '\n' + header + '\n' + source + '\n' + cases)
    for opt in ('-O0', '-O2'):
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-Wno-deprecated-declarations', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        opt, str(path / 'tests.cpp'), '-lcrypto', '-o', str(path / 'tests')], check=True)
        subprocess.run([str(path / 'tests')], check=True)
