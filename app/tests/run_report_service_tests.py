#!/usr/bin/env python3
"""Exercise production queue/coordinator with faulting storage and HTTP boundaries."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
production=(A/'sphaira/source/nexus/problem_reports.cpp').read_text()
production='\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
fixture=(A/'tests/report_service_fixture.hpp').read_text()+production+(A/'tests/report_service_cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-reports-') as tmp:
 T=Path(tmp);(T/'test.cpp').write_text(fixture)
 subprocess.run(['cc','-O2','-c',str(A/'vendor/yyjson/src/yyjson.c'),'-o',str(T/'yyjson.o')],check=True)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),'-I'+str(A/'vendor/yyjson/src'),str(T/'test.cpp'),str(T/'yyjson.o'),'-lcrypto','-o',str(T/'test')],check=True)
  subprocess.run([str(T/'test')],check=True)
