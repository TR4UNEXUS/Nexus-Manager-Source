"""Exercise the production inventory summary without reading game assets."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/game_cleanup.cpp').read_text(encoding='utf-8')
api=(root/'sphaira/include/nexus/manager.hpp').read_text(encoding='utf-8')
game=api[api.index('struct Game {'):api.index('struct Placeholder')]
scan=source[source.index('Result ScanGameLocations('):source.index('struct TicketRemovalState')]
fixture=r'''
#include <array>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <iostream>
constexpr const char* operator""_nx(const char* value,size_t){return value;}
using u64=uint64_t;using u32=uint32_t;using Result=u32;using Id=int;
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(false)
#define R_FAILED(x) ((x)!=0)
enum class Target{Sd,Internal,Usb};
enum {NcmContentMetaType_Application=128,NcmContentMetaType_Patch,NcmContentMetaType_AddOnContent,NcmContentMetaType_DataPatch};
// GAME
namespace ui{struct ProgressBox{Result cancel{};Result ShouldExitResult(){return cancel;}};}
struct Key{u64 id{};u32 version{};int type{};};
struct Record{Key key;u64 application;std::set<Id> ids;};
struct File{u64 size;};
struct Inventory{Target target;u64 generation;std::vector<Record> records;std::map<Id,File> files;};
Inventory disks[3];Result errors[3]{};
struct Session{Target target;u64 generation=42;ui::ProgressBox* p;explicit Session(ui::ProgressBox* box):p(box){};Result Open(Target t){target=t;return errors[int(t)];}Result Check(){return p->ShouldExitResult();}};
Result Database(Session& s,Inventory& i){i.records=disks[int(s.target)].records;return 0;}
Result PhysicalFiles(Session& s,Inventory& i,bool sizes=true){assert(sizes);i.files=disks[int(s.target)].files;return 0;}
const char* cleanup_stage="test";
namespace nexus{void Log(const char*,Result,u64=0){}}
// SCAN
int main(){
 disks[0].records={{{1,0,128},1,{1,2}},{{2,65536,129},1,{2,3}},{{4,0,130},1,{4}},{{5,1,131},1,{4,5}},{{6,131072,129},1,{6,7}}};
 disks[0].files={{1,{100}},{2,{200}},{3,{300}},{4,{400}},{5,{500}},{6,{600}}};
 disks[2].records={{{1,0,128},1,{1,2}},{{2,65536,129},1,{2,3}},{{4,0,130},1,{4}}};
 disks[2].files={{1,{100}},{2,{200}},{3,{300}},{4,{400}}};
 std::vector<Game> games(2);games[0].id=1;games[1].id=99;ui::ProgressBox p;
 assert(!ScanGameLocations(&p,games));const auto& g=games[0];
 assert(g.location_hint=="SD + USB"&&g.installed_bytes[0]==2100&&g.installed_bytes[2]==1000);
 assert(g.size_known[0]&&g.size_known[1]&&g.size_known[2]&&!g.location_checked);
 assert(g.has_base&&g.base_version==0&&g.has_update&&g.update_version==65536&&g.dlc_count==1);
 assert(g.bytes==std::array<u64,3>{}); // Information never authorizes a mutation.
 assert(games[1].location_hint=="Registered game files not found"&&games[1].installed_bytes[0]==0);
 errors[2]=99;assert(!ScanGameLocations(&p,games));assert(!games[0].size_known[2]&&games[0].installed_bytes[2]==0&&games[0].location_hint=="SD");
 assert(games[1].location_hint.find("0x00000063")!=std::string::npos);
 disks[0]={};errors[2]=0;disks[2]={};assert(!ScanGameLocations(&p,games));assert(!games[0].has_update&&!games[0].dlc_count&&!games[0].installed_bytes[0]);
 p.cancel=88;assert(ScanGameLocations(&p,games)==88);
 std::cout<<"Library details: physical sizes, unique NCAs, SD/USB copies, complete update revisions, DLC and unavailable storage passed\n";
}
'''.replace('// GAME',game).replace('// SCAN',scan).replace('assert(g.bytes==std::array<u64,3>{});','assert((g.bytes==std::array<u64,3>{}));')
with tempfile.TemporaryDirectory(prefix='nexus-library-details-') as directory:
 cpp=Path(directory)/'actual.cpp';cpp.write_text(fixture,encoding='utf-8')
 for mode in ('-O0','-O2'):
  binary=Path(directory)/('test'+mode)
  subprocess.run(['g++','-std=c++20',mode,'-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',str(cpp),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True,timeout=30)
