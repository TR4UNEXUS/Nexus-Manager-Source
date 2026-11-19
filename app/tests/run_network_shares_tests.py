from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
s=(root/'sphaira/source/nexus/network_shares.cpp').read_text()
start=s.index('Result ListNetworkShares(');opening=s.index('{',start);depth=1;end=opening+1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end].replace('std::chrono::steady_clock::now()','FakeClock::now()')
fixture=(root/'tests/network_shares_fixture.cpp').read_text().replace('// PRODUCTION',body)
with tempfile.TemporaryDirectory(prefix='nexus-network-shares-') as tmp:
 source=Path(tmp)/'actual.cpp';source.write_text(fixture)
 for mode in ('-O0','-O2'):
  exe=Path(tmp)/('tests'+mode)
  subprocess.run(['g++','-std=c++20',mode,'-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DENABLE_DEVOPTAB_SMB2','-DENABLE_DEVOPTAB_NFS','-I',str(root/'sphaira/include'),str(source),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
