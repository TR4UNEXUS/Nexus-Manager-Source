from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
production=(root/'sphaira/source/nexus/game_cleanup.cpp').read_text()
api=(root/'sphaira/include/nexus/game_cleanup.hpp').read_text()
def block(source,name):
 start=source.index(name);at=source.index('{',start);depth=1;end=at+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
fixture=(root/'tests/game_cleanup_fixture.cpp').read_text()
model='\n'.join(block(production,'struct '+name+' {')+';' for name in ('File','Record','Inventory'))
api=api[api.index('enum class CleanupKind'):api.index('// Inventory and preview')]
fixture=fixture.replace('// MODEL_PRODUCTION',model).replace('// API_PRODUCTION',api).replace('// STATE_PRODUCTION',block(production,'struct RemovalState {')+';')
functions='\n'.join(block(production,name) for name in ('std::set<Id> PhysicalIds(', 'std::string AppName(', 'Result ScanCleanup(', 'static Result PrepareGameRemoval(', 'Result PreviewGameRemoval(', 'Result ApplyGameRemoval(', 'Digest RemovalFingerprint(', 'Result PreviewCleanupBatch(', 'Result ApplyCleanupBatch('))
fixture=fixture.replace('// FUNCTIONS_PRODUCTION',functions)
with tempfile.TemporaryDirectory(prefix='nexus-game-cleanup-') as tmp:
 source=Path(tmp)/'actual.cpp';source.write_text(fixture)
 for optimization in ('-O0','-O2'):
  exe=Path(tmp)/('tests'+optimization)
  subprocess.run(['g++','-std=c++20',optimization,'-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I',str(root/'sphaira/include'),str(source),'-lcrypto','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=30)
