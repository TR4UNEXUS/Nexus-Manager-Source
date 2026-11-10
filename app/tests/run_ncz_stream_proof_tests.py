"""Compile actual NCZ read/decompression and header-proof capture with real crypto."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
yati=(root/'sphaira/source/yati/yati.cpp').read_text()
def block(marker):
    start=yati.index(marker);brace=yati.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(yati[end]=='{')-(yati[end]=='}');end+=1
    return yati[start:end]
fixture=(root/'tests/ncz_harness.cpp').read_text().replace('__declspec(dllexport)','')
fixture=fixture.replace('// READ_PRODUCTION',block('Result ThreadData::Read(')+'\n'+block('Result Yati::readFuncInternal('))
fixture=fixture.replace('// NCZ_PRODUCTION',block('Result Yati::decompressFuncInternal('))
cases=(root/'tests/ncz_stream_proof_tests.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-ncz-proof-') as directory:
    work=Path(directory);source=work/'production.cpp';source.write_text(fixture+'\n'+cases)
    for optimization in ('-O0','-O2'):
        binary=work/('proof'+optimization)
        subprocess.run(['g++','-std=c++23',optimization,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-Wno-deprecated-declarations','-Wno-sign-compare','-Wno-misleading-indentation',
                        '-I',str(root/'tests/compat-shim'),'-I',str(root/'sphaira/include'),
                        '-I','/opt/devkitpro/portlibs/switch/include',str(source),
                        '-l:libzstd.so.1','-lcrypto','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
