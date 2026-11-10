#!/usr/bin/env python3
"""Exercise the actual startup worker with real host threads and injected IPC."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/nexus/usb_library_startup.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
fixture = (root / 'tests/usb_library_startup_fixture.cpp').read_text().replace('// PRODUCTION_WORKER', source)
# Verify the production integration boundaries which make Stop's tested join
# happen before foreground storage workers and before app service teardown.
progress = (root / 'sphaira/source/ui/progress_box.cpp').read_text()
constructor = progress[progress.index('ProgressBox::ProgressBox('):progress.index('void ProgressBox::WorkerFinished')]
assert constructor.index('StopUsbLibraryStartupCheck()') < constructor.index('utils::CreateThread(')
app = (root / 'sphaira/source/app.cpp').read_text()
destructor = app[app.index('App::~App()'):]
assert destructor.index('StopUsbLibraryStartupCheck()') < destructor.index('m_widgets.pop_back()')
assert 'if(!nexus::lifecycle::CanPoll())nexusapp::CancelUsbLibraryStartupCheck();' in app
with tempfile.TemporaryDirectory(prefix='nexus-startup-library-') as temp:
    work = Path(temp)
    (work / 'test.cpp').write_text(fixture)
    for opt in ('-O0', '-O2'):
        subprocess.run(['g++', '-std=c++20', '-pthread', opt, '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', str(work / 'test.cpp'),
                        '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True)
