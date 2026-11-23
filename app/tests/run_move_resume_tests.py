from pathlib import Path
import re,subprocess,tempfile
A=Path(__file__).resolve().parents[1]
strip=lambda s:re.sub(r'^#(?:include|pragma)[^\n]*\n','',s,flags=re.M)
s=(A/'sphaira/source/nexus/manager.cpp').read_text()
parts=[s[s.index('constexpr Result Invalid ='):s.index('bool ForwarderAddressSpace(')],
       s[s.index('Result AppMetas('):s.index('class UsbSource final')],
       s[s.index('static Result RemoveVerifiedSourceInSession('):s.index('Result RemoveVerifiedSource(')]]
resume=s[s.index('Result ResumeMove('):s.index('Result ScanPlaceholders(')]
checkpoint=strip((A/'sphaira/include/nexus/move_checkpoint.hpp').read_text())
stub='''namespace sphaira::nexusapp {std::vector<MoveCheckpoint> saved;
void BackupConsoleRoutingAfterChange(){}
Result PendingMoves(std::vector<MoveCheckpoint>& out){R_TRY(event());out=saved;return 0;}
Result CompleteMoveCheckpoint(const MoveCheckpoint&){R_TRY(event());saved.clear();++complete_receipts;return 0;}
}'''
generated=(A/'tests/move_resume_fixture.hpp').read_text()+'\n#include "nexus/registration_protocol.hpp"\n'+checkpoint+stub+'\nnamespace sphaira::nexusapp {namespace {\n'+'\n'.join(parts)+'\n}\n'+resume+'\n}\n'+(A/'tests/move_resume_cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-move-resume-') as t:
 p=Path(t);(p/'test.cpp').write_text(generated)
 for opt in ('-O0','-O2'):
  subprocess.run(['c++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-variable','-Wno-deprecated-declarations','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(p/'test.cpp'),'-lcrypto','-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
