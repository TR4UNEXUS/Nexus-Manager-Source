#!/usr/bin/env python3
"""Compile actual import service acquisition/destruction methods with fault injection."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/usb_import.cpp').read_text()
def method(prefix):
 start=source.index(prefix);brace=source.index('{',start);end=brace+1;depth=1
 while depth:
  if source[end]=='{':depth+=1
  elif source[end]=='}':depth-=1
  end+=1
 return source[start:end]
fixture=(root/'tests/usb_import_services_tests.cpp').read_text()
fixture='#include <algorithm>\n'+fixture.replace('// PRODUCTION_OPEN',method('    Result Open(bool services)')).replace('// PRODUCTION_DESTRUCTOR',method('    ~Disk()'))
with tempfile.TemporaryDirectory(prefix='nexus-import-services-') as temp:
 p=Path(temp);(p/'test.cpp').write_text(fixture)
 for opt in ['-O0','-O2']:
  subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',opt,'-I',str(root/'sphaira/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

