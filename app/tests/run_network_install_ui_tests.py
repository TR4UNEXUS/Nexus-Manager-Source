"""Exercise actual NAS install UI methods with queued platform/service mocks."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
def extract(name, text=None):
    text = source if text is None else text
    start = text.index(name)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
fixture = (root / 'tests/network_install_ui_fixture.cpp').read_text()
outcome = (root / 'sphaira/include/nexus/network_install.hpp').read_text()
fixture = fixture.replace('// NETWORK_OUTCOME_PRODUCTION', extract('struct NetworkInstallOutcome', outcome) + ';')
fixture = fixture.replace('// QUEUE_UI_CASES', (root / 'tests/network_queue_ui_cases.cpp').read_text())
fixture = fixture.replace('// SESSION_PRODUCTION', extract('struct Session {') + ';')
methods = [extract(name) for name in ('void Message(', 'void PrepareInstall(', 'void ChooseInstallDestination(',
           'void PickPackage(')]
methods += [extract('struct NetworkQueueRecord') + ';', 'using NetworkQueueReport=std::vector<NetworkQueueRecord>;']
methods += [extract(name) for name in ('Result CheckInstallQueue(', 'Result RunInstallQueue(', 'std::string InstallQueueSummary(',
           'void PrepareInstallQueue(', 'void ChooseQueueDestination(', 'void Browse(SessionPtr session', 'void Connect(', 'bool PromptNetworkPassword(', 'void OpenSavedInstall(')]
form = source[source.index('class ConnectionForm final'):source.index('void DiscoverSmb(bool installing)')]
methods += [form, extract('class NetworkLocationsSidebar final') + ';', extract('void Locations(const std::string& protocol')]
fixture = fixture.replace('// FUNCTIONS_PRODUCTION', '\n'.join(methods))
fixture = fixture.replace('// DISPLAY_PRODUCTION', extract('void DisplayNetworkInstallLocations(') + '\n' + extract('void AddNetworkInstallShortcuts('))
menu = (root / 'sphaira/source/ui/menus/nexus_menu.cpp').read_text()
fixture = fixture.replace('// INSTALL_SOURCES_CLASS_PRODUCTION', extract('class InstallSourcesSidebar final', menu) + ';')
start = menu.index('            App::Push<InstallSourcesSidebar>')
end = menu.index('            });', start) + len('            });')
fixture = fixture.replace('// SOURCES_PRODUCTION', menu[start:end])
with tempfile.TemporaryDirectory(prefix='nexus-nas-ui-') as directory:
    work = Path(directory)
    test = work / 'production_ui.cpp'
    test.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('network_install_ui' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-DENABLE_DEVOPTAB_SMB2', '-DENABLE_DEVOPTAB_NFS',
                        '-I', str(root / 'sphaira/include'), '-I', str(root / 'tests'), str(test), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
