from pathlib import Path
import re,subprocess,tempfile
A=Path(__file__).resolve().parents[1]
strip=lambda s:re.sub(r'^#(?:include "|include <switch.h>|pragma)[^\n]*\n','',s,flags=re.M)
source=(A/'sphaira/source/nexus/routing_backup.cpp').read_text()
confirm=source[source.index('Result ConfirmConsoleRoutingBackup('):source.index('Result MissingRoutingWithConsoleBackup(')]
generated=(A/'tests/lr1_persistence_fixture.hpp').read_text()+strip((A/'sphaira/include/nexus/move_checkpoint.hpp').read_text())+strip((A/'sphaira/source/nexus/move_checkpoint.cpp').read_text())+'\nnamespace sphaira::nexusapp{'+confirm+'}\n'+(A/'tests/lr1_persistence_cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-persistence-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(generated)
 for opt in ('-O0','-O2'):
  subprocess.run(['c++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(p/'test.cpp'),'-lcrypto','-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
