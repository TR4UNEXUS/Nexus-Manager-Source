from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'sphaira/source/yati/yati.cpp').read_text()
a=s.index('Result InstallInternalStream(');b=s.index('\n} // namespace',a)
fixture=(root/'tests/stream_publish_fixture.cpp').read_text().replace('// PRODUCTION',s[a:b])
with tempfile.TemporaryDirectory(prefix='nexus-stream-publish-') as tmp:
 code=Path(tmp)/'test.cpp';code.write_text(fixture)
 for opt in ('-O0','-O2'):
  exe=Path(tmp)/('test'+opt)
  subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(root/'sphaira/include'),str(code),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
