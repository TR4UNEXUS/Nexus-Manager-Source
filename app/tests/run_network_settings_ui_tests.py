#!/usr/bin/env python3
"""Run the real sidebar constructor overloads and NAS settings form actions."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--expect-legacy-failure', action='store_true',
                    help='Prove the pre-fix literal-to-bool overload discards the form')
args = parser.parse_args()
network = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
sidebar = (root / 'sphaira/source/ui/sidebar.cpp').read_text()
header = (root / 'sphaira/include/ui/sidebar.hpp').read_text()

def block(source, name):
    start = source.index(name)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

classes = []
for name in ('class SidebarEntryCallback final', 'class SidebarEntryTextBase',
             'class SidebarEntryTextInput final'):
    item = block(header, name) + ';'
    item = re.sub(r'    void Draw\([^\n]*\n', '', item)
    classes.append(item)
constructors = sidebar[sidebar.index('SidebarEntryCallback::SidebarEntryCallback'):sidebar.index('void SidebarEntryCallback::Draw')]
constructors += sidebar[sidebar.index('SidebarEntryTextBase::SidebarEntryTextBase'):sidebar.index('void SidebarEntryTextBase::Draw')]
constructors += sidebar[sidebar.index('SidebarEntryTextInput::SidebarEntryTextInput'):sidebar.index('SidebarEntryFilePicker::SidebarEntryFilePicker')]

fixture = (root / 'tests/network_settings_ui_fixture.cpp').read_text()
fixture = fixture.replace('// PRODUCTION_ROW_CLASSES', '\n'.join(classes))
fixture = fixture.replace('// PRODUCTION_ROW_CONSTRUCTORS', constructors)
fixture = fixture.replace('// PRODUCTION_SESSION', block(network, 'struct Session {') + ';')
fixture = fixture.replace('// PRODUCTION_CONNECT', block(network, 'Result SaveConnectedLocation(') + '\n' + block(network, 'void Connect('))
fixture = fixture.replace('// PRODUCTION_PASSWORD_PROMPT', block(network, 'bool PromptNetworkPassword('))
fixture = fixture.replace('// PRODUCTION_FORM', block(network, 'class ConnectionForm final') + ';')
fixture = fixture.replace('// PRODUCTION_FAILURE_FORM', block(network, 'void EditConnectionAfterFailure(const Connection& c') )
with tempfile.TemporaryDirectory(prefix='nexus-network-settings-ui-') as directory:
    work = Path(directory)
    generated = work / 'production_ui.cpp'
    generated.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('settings' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-DENABLE_DEVOPTAB_SMB2', '-DENABLE_DEVOPTAB_NFS',
                        '-I', str(root / 'sphaira/include'), '-I', str(root / 'tests'), str(generated), '-o', str(binary)], check=True)
        subprocess.run([str(binary)] + (['legacy'] if args.expect_legacy_failure else []), check=True)
