"""Run the production startup/manual update UI with queued platform mocks."""
from pathlib import Path
import re
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
menu = (root / 'sphaira/source/ui/menus/nexus_menu.cpp').read_text()
header = (root / 'sphaira/include/ui/menus/nexus_menu.hpp').read_text()
release = (root / 'sphaira/source/nexus/release.cpp').read_text()
network = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
main = (root / 'sphaira/source/main.cpp').read_text()


def function(source, signature):
    """Retain an entire production function, including all nested callbacks."""
    start = source.index(signature)
    # Function bodies here have balanced braces in comments/string literals.
    opening = source.index('{', start)
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == '{':
            depth += 1
        elif source[position] == '}':
            depth -= 1
            if depth == 0:
                return source[start:position + 1]
    raise AssertionError(f'Unclosed function: {signature}')


# These boundary callsites cannot be simulated by the menu-only fixture. Keep
# narrow ordering checks alongside its production behavioral tests and the NAS
# fixture's actual Connect -> Session::Open ownership tests.
stop = 'nexus::updates::StopStartupCheck();'
pc_entry = function(function(menu, 'void Menu::ActivateMore()'), 'if(selected==0)')
assert pc_entry.index(stop) < pc_entry.index('App::Push<extended::Menu>(extended::Mode::Transfer)')
discovery = function(network, 'void DiscoverNetwork(bool installing,const std::string& protocol){')
assert discovery.index(stop) < discovery.index('std::make_shared<Session>()')
assert discovery.index(stop) < discovery.index('session->Open(')
shutdown = function(main, 'int main(')
assert shutdown.index('app->Loop();') < shutdown.index(stop) < shutdown.index('nexus::transfer::Stop();')
assert shutdown.index(stop) < shutdown.index('nexus::lifecycle::BeginShutdown();') < shutdown.index('app.reset();')
destructor = function(menu, 'Menu::~Menu()')
assert destructor.index(stop) < destructor.index('threadWaitForExit(')


production = '\n'.join(function(menu, signature) for signature in (
    'Menu::Menu(u32 flags)',
    'void Menu::CheckForUpdates()',
    'bool Menu::PollStartupUpdate()',
    'void Menu::ShowUpdatePrompt(',
    'void Menu::Update(Controller*',
))
state = re.findall(r'^\s*bool startup_update_.*?;.*$', header, re.MULTILINE)
assert sum(line.count('startup_update_') for line in state) == 2, \
    'The fixture must use both production startup state initializers'
fixture = (root / 'tests/startup_update_ui_fixture.cpp').read_text()
for marker, code in (
    ('// STARTUP_UPDATE_UI_PRODUCTION', production),
    ('// STARTUP_UPDATE_UI_STATE', '\n'.join(state)),
    ('// VERSION_CODE_PRODUCTION', function(release, 'std::uint32_t VersionCode(')),
):
    assert fixture.count(marker) == 1
    fixture = fixture.replace(marker, code)

with tempfile.TemporaryDirectory(prefix='nexus-startup-ui-') as directory:
    work = Path(directory)
    source = work / 'production_ui_tests.cpp'
    source.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('startup_update_ui' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG', '-Wall',
                        '-Wextra', '-Werror', '-Wno-misleading-indentation',
                        '-I', str(root / 'sphaira/include'),
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
