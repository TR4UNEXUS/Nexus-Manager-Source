"""Exercise the live session journal with real concurrent producers and readers."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-transfer-activity-') as directory:
    for opt in ('-O0','-O2'):
        exe=Path(directory)/('test'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
            '-UNDEBUG','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
            '-I',str(root/'sphaira/include'),str(root/'tests/transfer_activity_tests.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
