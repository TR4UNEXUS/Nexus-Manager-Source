#!/usr/bin/env python3
"""Exercise actual transfer rendering with deterministic font/drawing probes."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/ui/progress_box.cpp').read_text()
def block(signature):
    start=source.index(signature);brace=source.index('{',start);depth,end=1,brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=(root/'tests/progress_layout_fixture.cpp').read_text()
activity_ui=(root/'sphaira/include/ui/transfer_activity.hpp').read_text()
fixture=fixture.replace('// INSTALL_VIEW_PRODUCTION',activity_ui[activity_ui.index('struct InstallView {'):activity_ui.index('inline void Line(')])
fixture=fixture.replace('// HELPERS_PRODUCTION',block('void DrawProgressLine(')+'\n'+block('void DrawProgressBody('))
fixture=fixture.replace('// DRAW_PRODUCTION',block('auto ProgressBox::Draw('))
output=root/'build/network-browser-validation/progress-layout'
output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='nexus-progress-layout-') as directory:
    work=Path(directory);generated=work/'layout.cpp';generated.write_text(fixture)
    for opt in ('-O0','-O2'):
        binary=work/('layout'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror',
            '-Wno-unused-parameter','-Wno-misleading-indentation','-fsanitize=undefined',
            '-I',str(root/'sphaira/include'),str(generated),'-o',str(binary)],check=True)
        subprocess.run([str(binary),str(output)],check=True)
print('SVG layout probes:',output)
