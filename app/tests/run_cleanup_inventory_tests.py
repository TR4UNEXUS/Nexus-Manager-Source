from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
production=(root/'sphaira/source/nexus/game_cleanup.cpp').read_text()
def block(name):
 start=production.index(name);at=production.index('{',start);depth=1;end=at+1
 while depth:
  depth+=(production[end]=='{')-(production[end]=='}');end+=1
 return production[start:end]
fixture=(root/'tests/cleanup_inventory_fixture.cpp').read_text().replace('// RECORD_PRODUCTION',block('struct Record {')+';').replace('// PARSER_PRODUCTION',block('u64 Parent(')+'\n'+block('Result Database('))
with tempfile.TemporaryDirectory(prefix='nexus-inventory-') as tmp:
 source=Path(tmp)/'actual.cpp';source.write_text(fixture)
 for mode in ('-O0','-O2'):
  exe=Path(tmp)/('tests'+mode)
  subprocess.run(['g++','-std=c++20',mode,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I',str(root/'sphaira/include'),str(source),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
