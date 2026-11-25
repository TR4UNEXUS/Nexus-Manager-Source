"""Run the actual USB import UI workflow with queued platform/UI mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
production = (root / 'sphaira/source/ui/menus/usb_import_menu.cpp').read_text()
# Keep the complete production flow; replace only platform type declarations.
production = '\n'.join(line for line in production.splitlines()
                       if not line.lstrip().startswith('#include'))
fixture = (root / 'tests/usb_import_ui_fixture.cpp').read_text()
cases = (root / 'tests/usb_import_guided_ui_cases.cpp').read_text()
menu = (root / 'sphaira/source/ui/menus/nexus_menu.cpp').read_text()

def function(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end]

startup_header = (root / 'sphaira/include/nexus/usb_library_startup.hpp').read_text()
startup_header = '\n'.join(line for line in startup_header.splitlines()
                           if not line.lstrip().startswith('#'))
fixture = fixture.replace('// USB_LIBRARY_STARTUP_HEADER', startup_header)
cases = cases.replace('// STARTUP_USB_PRODUCTION',function(menu, 'void Menu::OpenUsbRepair()'))
assert 'PollUsbLibraryStartup' not in menu and 'StartUsbLibraryStartupCheck()' not in menu
assert 'USB games need attention' not in menu
assert 'StopUsbLibraryStartupCheck();' in function(production, 'void DisplayUsbImport()')
actions_start = menu.index('    if(page==Page::Installed){', menu.index('void Menu::SetupActions()'))
actions = function(menu[actions_start:], '    if(page==Page::Installed){')
cases = cases.replace('// LIBRARY_INSTALLED_ACTIONS', actions[actions.index('{')+1:-1])
touch_start = menu.index('            if(touch->in_range({850,110,376,46}))')
cases = cases.replace('// LIBRARY_IMPORT_TOUCH', menu[touch_start:menu.index('\n', touch_start)])
draw_start = menu.index('        Text(vg,54,116,28,', menu.index('void Menu::Draw('))
draw_end = menu.index('        if(games.empty())', draw_start)
cases = cases.replace('// LIBRARY_IMPORT_DRAW', menu[draw_start:draw_end])
assert menu.index('nexusdialog::Button(vg,{850,110,376,46}', draw_start) < draw_end
assert '"Import USB games into this system",[]{nexusapp::DisplayUsbImport();}' not in menu
assert '"Installed games"' in menu and '"My games"' not in menu
fixture = fixture.replace('// USB_IMPORT_GUIDED_CASES', cases)
assert fixture.count('// USB_IMPORT_UI_PRODUCTION') == 1
icons = (root / 'sphaira/include/ui/usb_import_icons.hpp').read_text()
fixture = fixture.replace('// USB_ICON_CACHE_PRODUCTION', '\n'.join(line for line in icons.splitlines() if not line.lstrip().startswith('#')))
fixture = fixture.replace('// USB_IMPORT_UI_PRODUCTION', production)
network = (root / 'sphaira/source/nexus/network_locations.cpp').read_text()
session = network[network.index('struct Session {'):network.index('using SessionPtr=')]
save = network[network.index('std::vector<Connection> Load()'):network.index('struct Hash {')]
fixture = fixture.replace('// NETWORK_SESSION_PRODUCTION', session + '\n' + save)
with tempfile.TemporaryDirectory(prefix='nexus-import-ui-') as directory:
    work = Path(directory)
    source = work / 'production_ui_tests.cpp'
    source.write_text(fixture)
    for optimization in ('-O0', '-O2'):
        binary = work / ('usb_import_ui' + optimization)
        subprocess.run(['g++', '-std=c++20', optimization, '-UNDEBUG',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Wall',
                        '-Wextra', '-Werror', '-Wno-misleading-indentation',
                        '-I', str(root / 'sphaira/include'),
                        '-I', str(root / 'tests'),
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
