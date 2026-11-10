#!/usr/bin/env python3
"""Clock/byte telemetry plus actual ProgressBox setter/cancellation behavior."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/ui/progress_box.cpp').read_text()
def block(source,start):
    i=source.index(start);b=source.index('{',i);d,e=1,b+1
    while d:
        d+=(source[e]=='{')-(source[e]=='}');e+=1
    return source[i:e]
names=('auto ProgressBox::NewTransfer(', 'auto ProgressBox::ResetTranfser(',
       'auto ProgressBox::UpdateTransfer(', 'auto ProgressBox::SetOverallTransfer(',
       'auto ProgressBox::SetDetailedProgress(', 'auto ProgressBox::SetProgressNote(',
       'void ProgressBox::BeginNetworkTransfer(', 'void ProgressBox::AddNetworkBytes(',
       'void ProgressBox::EndNetworkTransfer(', 'void ProgressBox::RequestExit(',
       'void ProgressBox::WorkerFinished(')
methods=[block(source,n) for n in names]
declarations=[m[:m.index('{')].replace('ProgressBox::','')+';' for m in methods]
fixture=(root/'tests/network_speed_fixture.cpp').read_text()
fixture=fixture.replace('// DECLARATIONS','\n'.join(declarations)).replace('// METHODS','\n'.join(methods))
with tempfile.TemporaryDirectory(prefix='nexus-network-speed-') as directory:
    work=Path(directory);generated=work/'speed.cpp';generated.write_text(fixture)
    for opt in ('-O0','-O2'):
        binary=work/('speed'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',
            '-Wno-sign-compare','-fsanitize=undefined','-pthread','-I',str(root/'sphaira/include'),str(generated),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
