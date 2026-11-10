#!/usr/bin/env python3
"""Run the real ES ticket parser with exact ticket structs under ASan/UBSan."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/yati/nx/es.cpp').read_text()
header = (root / 'sphaira/include/yati/nx/es.hpp').read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


types = header[header.index('enum TitleKeyType'):header.index('struct EticketRsaDeviceKey')]
functions = extract('Result GetTicketDataOffset(') + '\n' + extract('Result GetTicketData(')
fixture = (root / 'tests/es_ticket_parser_fixture.cpp').read_text()
unit = fixture.replace('// PRODUCTION_TYPES', types).replace('// PRODUCTION_FUNCTIONS', functions)
environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
with tempfile.TemporaryDirectory(prefix='nexus-es-ticket-parser-') as directory:
    work = Path(directory)
    code = work / 'actual_parser.cpp'
    code.write_text(unit)
    for optimization in ('-O0', '-O2'):
        binary = work / ('parser' + optimization)
        subprocess.run(['g++', '-std=c++23', optimization, '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                        str(code), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env=environment)
    # Prove this regression detects the original unsafe four-byte signature read.
    guard = '    out = 0;\n    R_UNLESS(ticket.size() >= sizeof(u32), Result_EsBadTicketSize);\n'
    assert functions.count(guard) == 1
    old = work / 'unguarded_parser.cpp'
    old.write_text(unit.replace(guard, '', 1))
    binary = work / 'unguarded_parser'
    subprocess.run(['g++', '-std=c++23', '-O1', '-g', '-fsanitize=address,undefined', '-no-pie',
                    str(old), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), 'short-ticket'], text=True, capture_output=True, env=environment)
    assert result.returncode != 0 and 'AddressSanitizer' in result.stderr
    assert 'GetTicketDataOffset' in result.stderr
    print('Regression confirmed: original one-byte ticket read is rejected by ASan; guarded parser passes.')
