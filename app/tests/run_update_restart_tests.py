#!/usr/bin/env python3
"""Exercise the production edition-specific restart helper, including failures."""
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
production=(root/'sphaira/source/nexus/update_restart.cpp').read_text()
production=re.sub(r'^#include "[^\n]*\n','',production,flags=re.M)
fixture=(root/'tests/update_restart_fixture.cpp').read_text().replace('// RESTART_PRODUCTION',production)
cases=('native','native-launch-failure','native-wrong-id','native-info-failure',
       'native-applet','native-system-app','nro','nro-launch-failure','nro-wrong-path',
       'nro-applet','nro-missing','nro-directory','nro-corrupt','nro-short')
with tempfile.TemporaryDirectory(prefix='nexus-update-restart-') as directory:
    work=Path(directory)
    source=work/'test.cpp';source.write_text(fixture)
    for optimization in ('-O0','-O2'):
        binary=work/('test'+optimization)
        subprocess.run(['g++','-std=c++20',optimization,'-g','-UNDEBUG','-Wall','-Wextra','-Werror',
            '-fsanitize=undefined','-fno-sanitize-recover=all',str(source),'-o',str(binary)],check=True)
        for case in cases:
            folder=work/(case+optimization);folder.mkdir()
            subprocess.run([str(binary),case],cwd=folder,check=True,timeout=5)
print('Production update restart tests passed at -O0 and -O2.')
