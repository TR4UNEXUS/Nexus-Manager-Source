#!/usr/bin/env python3
"""Exercise the production NAS install adapter against faults and an install spy."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/network_install.cpp').read_text()
source = re.sub(r'^#include "[^\n]*\n', '', source, flags=re.M)
fixture = (root / 'tests/network_install_fixture.hpp').read_text()
network_header = (root / 'sphaira/include/nexus/network_install.hpp').read_text()
network_outcome = re.search(r'struct NetworkInstallOutcome \{[^\n]+;', network_header).group()
network_declaration = re.search(r'Result InstallNetworkPackage\([^;]+;', network_header).group()
fixture = fixture.replace('// NETWORK_OUTCOME_PRODUCTION', network_outcome + '\n' + network_declaration)
installer = (root / 'sphaira/source/yati/yati.cpp').read_text()
filters = '\n'.join(re.findall(r'^    config\.(?:ticket_only|skip_base|skip_patch|skip_addon|skip_data_patch|skip_ticket) = [^\n]+;', installer, re.M))
assert len(filters.splitlines()) == 6
fixture = fixture.replace('// YATI_FILTER_SETUP_PRODUCTION', filters)
def extract(name):
    start = installer.index(name)
    begin = installer.index('{', start)
    depth, end = 1, begin + 1
    while depth:
        depth += (installer[end] == '{') - (installer[end] == '}')
        end += 1
    return installer[start:end]
policy = (root / 'tests/network_install_policy_fixture.cpp').read_text()
header = (root / 'sphaira/include/yati/yati.hpp').read_text()
outcome = re.search(r'struct InstallOutcome \{[^\n]+;', header).group()
policy = policy.replace('// OUTCOME_PRODUCTION', outcome)
policy = policy.replace('// FILTER_SETUP_PRODUCTION', filters)
policy = policy.replace('// GET_LATEST_PRODUCTION', extract('Result Yati::GetLatestVersion('))
policy = policy.replace('// SHOULD_SKIP_PRODUCTION', extract('Result Yati::ShouldSkip('))
policy = policy.replace('// INSTALL_INTERNAL_PRODUCTION', extract('Result InstallInternal(ui::ProgressBox*'))
tests = (root / 'tests/network_install_tests.cpp').read_text()
route = (root / 'tests/network_container_route_fixture.cpp').read_text()
route = route.replace('// ROUTE_PRODUCTION', extract('Result InstallFromSource(ui::ProgressBox*'))
with tempfile.TemporaryDirectory(prefix='nexus-network-install-') as directory:
    work = Path(directory)
    generated = work / 'production.cpp'
    generated.write_text(fixture + '\n' + source + '\n' + tests)
    policy_file = work / 'actual_installer_policy.cpp'
    policy_file.write_text(policy)
    route_file = work / 'actual_container_route.cpp'
    route_file.write_text(route)
    for optimization in ('-O0', '-O2'):
        for index, code in enumerate((generated, policy_file, route_file)):
            binary = work / ('tests' + str(index) + optimization)
            subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                            '-Wno-misleading-indentation', '-Wno-sign-compare', '-Wno-missing-field-initializers', '-pthread', '-I', str(root / 'sphaira/include'),
                            str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
