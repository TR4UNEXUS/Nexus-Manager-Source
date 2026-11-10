#!/usr/bin/env python3
"""Check the production bounded-label algorithm and main-menu column spacing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'sphaira/source/ui/menus/nexus_menu.cpp').read_text()
start = source.index('template<class Measure>\nstatic std::string FitLineText(')
end = source.index('static void BoundedLine(', start)
fit = source[start:end]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "nexus/file_manager_core.hpp"
// FIT_PRODUCTION
int main(){
    unsigned calls{};
    const auto measure=[&](std::string_view s){++calls;float width{};for(unsigned char c:s)if((c&0xc0)!=0x80)width+=10;return width;};
    assert(FitLineText("",100,measure).empty());
    assert(FitLineText("game.nsp",0,measure).empty());
    assert(FitLineText("game.nsp",80,measure)=="game.nsp");
    assert(FitLineText("game.nsp",79,measure)=="game...");
    assert(FitLineText("game.nsp",20,measure).empty());
    assert(FitLineText("game.nsp",30,measure)=="...");
    for(const std::string text:{"Long game update title and version.nsp", "\xc3\xa9\xe6\x97\xa5\xe6\x9c\xac\xf0\x9f\x8e\xae game.xci"}){
        for(float width=0;width<500;width+=7){const auto line=FitLineText(text,width,measure);
            assert(nexus::files::Utf8(line));assert(measure(line)<=width);
            if(!line.empty()&&line!=text){assert(line.ends_with("..."));assert(text.starts_with(line.substr(0,line.size()-3)));}}
    }
    calls=0;const auto long_line=FitLineText(std::string(10000,'X'),500,measure);assert(long_line.size()==50&&calls<20);
    std::cout<<"Main labels: bounded width, readable fixed font, UTF-8-safe ellipsis and logarithmic long-label fitting passed\n";
}
'''.replace('// FIT_PRODUCTION', fit)
# Production draw rectangles reserve an explicit gap between names and status.
assert 'name_line({128,y,736,56}' in source and 'BoundedLine(vg,{888,y,320,56}' in source
assert 128 + 736 + 24 == 888 and 888 + 320 == 1208
assert 'name_line({text_x,y,976-text_x,56}' in source and 'BoundedLine(vg,{1000,y,208,56}' in source
assert 976 + 24 == 1000 and 1000 + 208 == 1208
assert 'name_line({text_x,y,832-text_x,56}' in source and 'BoundedLine(vg,{856,y,352,56}' in source
assert 832 + 24 == 856 and 856 + 352 == 1208
assert 'row_text_scroll.Draw(vg,true' in source and 'path_text_scroll.Draw(vg,true' in source
assert 'nvgIntersectScissor(vg,r.x,r.y,r.w,r.h)' in source
for slot in range(7):
    top = 174 + slot * 60
    assert top >= 174 and top + 56 <= 590
assert 590 < 602 and 602 + 31 < 639
with tempfile.TemporaryDirectory(prefix='nexus-main-text-layout-') as directory:
    work = Path(directory)
    generated = work / 'labels.cpp'
    generated.write_text(fixture)
    for input_file in (generated, root / 'tests/manager_layout_tests.cpp'):
        binary = work / input_file.stem
        subprocess.run(['g++', '-std=c++20', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-I', str(root / 'sphaira/include'),
                        str(input_file), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Main list columns, row spacing, selected-name/path scrolling and footer separation passed')
