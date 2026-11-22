#!/usr/bin/env python3
"""Compile the real i18n implementation against an in-memory filesystem."""
from pathlib import Path
import subprocess,tempfile,os
A=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-language-') as tmp:
 T=Path(tmp);source=(A/'sphaira/source/i18n.cpp').read_text()
 source=source.replace('#include "fs.hpp"','#include "language_runtime_fixture.hpp"').replace('#include "log.hpp"','')
 (T/'i18n.cpp').write_text(source)
 subprocess.run(['cc','-c','-O1',str(A/'vendor/yyjson/src/yyjson.c'),'-o',str(T/'yyjson.o')],check=True)
 subprocess.run(['c++','-std=c++23','-O1','-g','-D__SWITCH__','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(A/'tests'),'-I'+str(A/'sphaira/include'),'-I'+str(A/'vendor/yyjson/src'),str(T/'i18n.cpp'),str(A/'tests/language_tests.cpp'),str(T/'yyjson.o'),'-pthread','-o',str(T/'languages')],check=True)
 # Disable ASan's signal interception on WSL; instrumentation remains enabled.
 env=dict(os.environ,ASAN_OPTIONS='handle_segv=0:abort_on_error=1')
 subprocess.run([str(T/'languages')],check=True,env=env,timeout=30)
