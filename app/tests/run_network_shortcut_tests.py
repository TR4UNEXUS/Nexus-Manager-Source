#!/usr/bin/env python3
"""Exercise actual saved network shortcut, form and credential routing code."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
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

fixture = (root / 'tests/network_settings_ui_fixture.cpp').read_text().split('int main(')[0]
classes = []
for name in ('class SidebarEntryCallback final', 'class SidebarEntryTextBase', 'class SidebarEntryTextInput final'):
    classes.append(re.sub(r'    void Draw\([^\n]*\n', '', block(header, name) + ';'))
constructors = sidebar[sidebar.index('SidebarEntryCallback::SidebarEntryCallback'):sidebar.index('void SidebarEntryCallback::Draw')]
constructors += sidebar[sidebar.index('SidebarEntryTextBase::SidebarEntryTextBase'):sidebar.index('void SidebarEntryTextBase::Draw')]
constructors += sidebar[sidebar.index('SidebarEntryTextInput::SidebarEntryTextInput'):sidebar.index('SidebarEntryFilePicker::SidebarEntryFilePicker')]
fixture = fixture.replace('// PRODUCTION_ROW_CLASSES', '\n'.join(classes))
fixture = fixture.replace('// PRODUCTION_ROW_CONSTRUCTORS', constructors)
fixture = fixture.replace('// PRODUCTION_SESSION', block(network, 'struct Session {') + ';')
fixture = fixture.replace('// PRODUCTION_CONNECT', block(network, 'void Connect('))
fixture = fixture.replace('// PRODUCTION_PASSWORD_PROMPT', block(network, 'bool PromptNetworkPassword('))
fixture = fixture.replace('// PRODUCTION_FORM', block(network, 'class ConnectionForm final') + ';')
fixture = fixture.replace('// PRODUCTION_FAILURE_FORM', block(network, 'void EditConnectionAfterFailure(const Connection& c'))
for helper in ('void Fill(', 'void AssertValues('):
    fixture = fixture.replace(block(fixture, helper), '')
cases = (root / 'tests/network_shortcut_tests.cpp').read_text()
cases = cases.replace('// OPEN_SAVED_PRODUCTION', block(network, 'void OpenSavedInstall('))
cases = cases.replace('// ADD_SHORTCUTS_PRODUCTION', block(network, 'void AddNetworkInstallShortcuts('))
menu = (root / 'sphaira/source/ui/menus/nexus_menu.cpp').read_text()
assert 'devoptab::AddNetworkInstallShortcuts(sources);' in menu
with tempfile.TemporaryDirectory(prefix='nexus-network-shortcuts-') as directory:
    source = Path(directory) / 'production.cpp'
    source.write_text(fixture + '\n' + cases)
    for optimization in ('-O0', '-O2'):
        binary = Path(directory) / ('shortcuts' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-DENABLE_DEVOPTAB_SMB2', '-DENABLE_DEVOPTAB_NFS',
                        '-I', str(root / 'sphaira/include'), '-I', str(root / 'tests'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
