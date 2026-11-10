#!/usr/bin/env python3
"""Exercise production browser controller/draw methods and shared navigation state."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
header=(root/'sphaira/include/ui/menus/network_browser.hpp').read_text()
declaration=header[header.index('class Browser final'):header.rindex('\n}')]
source=(root/'sphaira/source/ui/menus/network_browser.cpp').read_text()
source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
fixture=(root/'tests/network_browser_fixture.cpp').read_text().replace('// BROWSER_CLASS',declaration).replace('// BROWSER_SOURCE',source)
with tempfile.TemporaryDirectory(prefix='nexus-fullscreen-browser-') as directory:
    folder=Path(directory);test=folder/'test.cpp';test.write_text(fixture)
    for opt in ('-O0','-O2'):
        binary=folder/('browser'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I',str(root/'sphaira/include'),str(test),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
