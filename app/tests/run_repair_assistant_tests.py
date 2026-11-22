#!/usr/bin/env python3
"""Compile the complete production repair menu with queued UI/service boundaries."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
p=(A/'sphaira/source/ui/menus/repair_assistant_menu.cpp').read_text()
p='\n'.join(line for line in p.splitlines() if not line.startswith('#include'))
fixture=(A/'tests/repair_assistant_fixture.hpp').read_text()+p+(A/'tests/repair_assistant_cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-repair-ui-') as tmp:
 T=Path(tmp);(T/'test.cpp').write_text(fixture)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(T/'test.cpp'),'-o',str(T/'test')],check=True)
  subprocess.run([str(T/'test')],check=True)
