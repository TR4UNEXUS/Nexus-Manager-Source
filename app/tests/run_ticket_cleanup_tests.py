"""Run the production ticket inventory/removal and label code with fake services."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/game_cleanup.cpp').read_text()
api=(root/'sphaira/include/nexus/game_cleanup.hpp').read_text()
def block(name):
 start=source.index(name);at=source.index('{',start);depth=1;end=at+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
fixture=(root/'tests/ticket_cleanup_fixture.cpp').read_text()
models='\n'.join(block('struct '+name+' {')+';' for name in ('File','Record','Inventory','TicketList','TicketRemovalState'))
fixture=fixture.replace('// MODEL_PRODUCTION',models).replace('// API_PRODUCTION',api[api.index('enum class CleanupKind'):api.index('// Inventory and preview')])
functions='\n'.join(block(name) for name in ('Result FileMatches(', 'Result ReadTickets(', 'Result ReadRights(', 'Result MissingTicketGames(', 'Result TicketInventory(', 'bool UnusedTicket(', 'Result PreviewTicketRemoval(', 'Result ApplyTicketRemoval(', 'Result ScanUnusedTickets(', 'Result ScanGameLocations(', 'Result KeepSharedRecord(', 'Result RemovedFileAbsent('))
functions+='\n'+block('Result PhysicalFiles(').replace('Result PhysicalFiles(', 'Result InventoryPhysicalFiles(')
fixture=fixture.replace('// FUNCTIONS_PRODUCTION',functions)
with tempfile.TemporaryDirectory(prefix='nexus-ticket-cleanup-') as directory:
 work=Path(directory);cpp=work/'test.cpp';cpp.write_text(fixture)
 for opt in ('-O0','-O2'):
  exe=work/('test'+opt)
  subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(root/'sphaira/include'),str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
