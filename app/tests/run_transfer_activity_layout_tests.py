"""Exercise actual live-view drawing with NanoVG; emit a text/layout preview."""
from pathlib import Path
import re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
strip=lambda text:re.sub(r'^#(?:include|pragma)[^\n]*\n','',text,flags=re.M)
fixture=(root/'tests/transfer_activity_layout_fixture.cpp').read_text()
fixture=fixture.replace('// VIOLET',strip((root/'sphaira/include/ui/nexus_violet.hpp').read_text()))
fixture=fixture.replace('// TRANSFER_UI',strip((root/'sphaira/include/ui/transfer_activity.hpp').read_text()))
font=Path('/mnt/c/Windows/Fonts/segoeui.ttf')
if not font.exists():font=Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
output=Path(sys.argv[1]);output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='nexus-live-layout-') as directory:
    work=Path(directory);cpp=work/'test.cpp';cpp.write_text(fixture)
    includes=['-I',str(root/'sphaira/include'),'-I',str(root/'vendor/nanovg/include'),'-I',str(root/'vendor/nanovg/include/nanovg'),'-I',str(root/'vendor/stb_nanovg')]
    obj=work/'nanovg.o'
    subprocess.run(['gcc','-O2','-DNVG_NO_STB',*includes,'-c',str(root/'vendor/nanovg/source/nanovg.c'),'-o',str(obj)],check=True)
    for opt in ('-O0','-O2'):
        binary=work/('test'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra',*includes,str(cpp),str(obj),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary),str(font),str(output)],check=True)
