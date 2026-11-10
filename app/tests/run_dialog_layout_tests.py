#!/usr/bin/env python3
"""Run production dialog drawing and controls with real NanoVG text metrics."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def strip(text):return re.sub(r'^#(?:include|pragma)[^\n]*\n','',text,flags=re.M)
def block(text,name):
    start=text.index(name);opening=text.index('{',start);end=opening+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
fixture=(root/'tests/dialog_layout_fixture.cpp').read_text()
fixture=fixture.replace('// VIOLET',strip((root/'sphaira/include/ui/nexus_violet.hpp').read_text()))
fixture=fixture.replace('// DIALOG',strip((root/'sphaira/include/ui/nexus_dialog.hpp').read_text()))
sidebar=(root/'sphaira/source/ui/sidebar.cpp').read_text()
fixture=fixture.replace('// SIDEBAR_DRAW','namespace sphaira::ui {\n'+block(sidebar,'void SidebarEntryBase::Draw(')+'\n'+block(sidebar,'void SidebarEntryBase::DrawEntry(')+'\n}')
fixture=fixture.replace('// OPTION_HEADER',strip((root/'sphaira/include/ui/option_box.hpp').read_text()))
fixture=fixture.replace('// OPTION_SOURCE',strip((root/'sphaira/source/ui/option_box.cpp').read_text()))
fixture=fixture.replace('// ERROR_HEADER',strip((root/'sphaira/include/ui/error_box.hpp').read_text()))
error=(root/'sphaira/source/ui/error_box.cpp').read_text()
constructor=error[error.index('ErrorBox::ErrorBox(const std::string& message)'):error.index('ErrorBox::ErrorBox(Result code')]
fixture=fixture.replace('// ERROR_METHODS','namespace sphaira::ui {\n'+constructor+'\n'+'\n'.join(block(error,name) for name in ('auto ErrorBox::Update(', 'auto ErrorBox::Draw('))+'\n}')
font=Path('/mnt/c/Windows/Fonts/segoeui.ttf')
if not font.exists():font=Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
assert font.exists(),'A real TrueType font is required'
with tempfile.TemporaryDirectory(prefix='nexus-modal-layout-') as directory:
    work=Path(directory);test=work/'test.cpp';test.write_text(fixture)
    obj=work/'nanovg.o';includes=['-I',str(root/'vendor/nanovg/include'),'-I',str(root/'vendor/nanovg/include/nanovg'),'-I',str(root/'vendor/stb_nanovg')]
    subprocess.run(['gcc','-O2','-DNVG_NO_STB',*includes,'-c',str(root/'vendor/nanovg/source/nanovg.c'),'-o',str(obj)],check=True)
    for opt in ('-O0','-O2'):
        binary=work/('dialog'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-Wno-misleading-indentation',*includes,str(test),str(obj),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary),str(font)],check=True)
