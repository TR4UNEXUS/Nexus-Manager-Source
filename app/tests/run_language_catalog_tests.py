#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,os
A=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-catalogues-') as tmp:
 T=Path(tmp);source=(A/'sphaira/source/i18n.cpp').read_text()
 source=source.replace('#include "fs.hpp"','#include "language_runtime_fixture.hpp"').replace('#include "log.hpp"','')
 (T/'i18n.cpp').write_text(source)
 subprocess.run(['cc','-c','-O1',str(A/'vendor/yyjson/src/yyjson.c'),'-o',str(T/'yyjson.o')],check=True)
 subprocess.run(['c++','-std=c++23','-O1','-D__SWITCH__','-fsanitize=undefined','-I'+str(A/'tests'),'-I'+str(A/'sphaira/include'),'-I'+str(A/'vendor/yyjson/src'),str(T/'i18n.cpp'),str(A/'tests/language_catalog_tests.cpp'),str(T/'yyjson.o'),'-pthread','-o',str(T/'catalogues')],check=True)
 subprocess.run([str(T/'catalogues'),str(A/'assets/romfs/i18n')],check=True,timeout=90)
 staged=T/'usbloader-romfs'
 subprocess.run(['cmake','-DSOURCE_DIR='+str(A),'-DDEST='+str(staged),'-P',str(A/'cmake/UsbLoaderAssets.cmake')],check=True)
 originals={p.name:p.read_bytes() for p in (A/'assets/romfs/i18n').glob('*.json')}
 packaged={p.name:p.read_bytes() for p in (staged/'i18n').glob('*.json')}
 assert originals==packaged, 'Packaging must preserve translation keys and values exactly'
 subprocess.run([str(T/'catalogues'),str(staged/'i18n')],check=True,timeout=90)
 print('Source and staged catalogues passed unchanged.')
