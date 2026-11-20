"""Run the production port-mod importer with memory-backed disk boundaries.
ZIP parsing, path validation, CRC checks and copying are the actual app code.
"""
from pathlib import Path
import tempfile,subprocess,zipfile
app=Path(__file__).resolve().parents[1]
production=(app/'sphaira/source/nexus/port_manager.cpp').read_text()
start=production.index('struct ZipReader ')
end=production.index('\n}\nResult ImportMod(',start)
functions=production[start:end]
start=production.index('Result ImportMod(');at=production.index('{',start);depth=1;end=at+1
while depth:
 depth+=(production[end]=='{')-(production[end]=='}');end+=1
functions+=production[start:end]
harness=r'''
#include "nexus/port_manager_core.hpp"
#include "nexus/task_progress.hpp"
#include <minizip/unzip.h>
#include <openssl/sha.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <iostream>
using u8=unsigned char;using u64=std::uint64_t;using s64=std::int64_t;using Result=unsigned;
constexpr Result Bad=99;
#define R_TRY(x) {const auto r_=(x);if(r_)return r_;}
#define R_UNLESS(x,r) {if(!(x))return r;}
#define R_FAILED(x) ((x)!=0)
namespace core=nexus::ports;namespace f=nexus::files;
namespace ui {
struct ProgressBox {nexus::TaskProgress state;unsigned last{};
 void NewTransfer(const std::string&){}
 void UpdateTransfer(u64 done,u64 total){state.Set(done,total);assert(state.value>=last&&state.value<10000);last=state.value;}
};
struct ProgressScope {ProgressBox* p;ProgressScope(ProgressBox* b,unsigned a,unsigned z):p(b){p->state.Push(a,z);}~ProgressScope(){p->state.Pop();}};
}
namespace nexusapp {struct Item{std::string name,path;u64 size{};bool directory{};};std::string HexId(const u8* p,size_t n=32){const char* h="0123456789abcdef";std::string s;for(size_t i=0;i<n;++i){s+=h[p[i]>>4];s+=h[p[i]&15];}return s;}}
namespace nexusfiles {struct Selection{bool usb;u64 generation;nexusapp::Item item;};}
struct Port{std::string name,nro;bool usb;u64 generation,application;std::vector<std::string> roots;};
struct Mod{std::string name,path,root;bool active,pending;u64 bytes;size_t files;};
struct Info{bool exists{},directory{};u64 size{};};
struct Sha256Context{std::vector<u8> data;};
void sha256ContextCreate(Sha256Context* c){c->data.clear();}
void sha256ContextUpdate(Sha256Context* c,const void* p,size_t n){auto* b=static_cast<const u8*>(p);c->data.insert(c->data.end(),b,b+n);}
void sha256ContextGetHash(Sha256Context* c,void* p){SHA256(c->data.data(),c->data.size(),static_cast<u8*>(p));}
struct Node{bool dir{};std::vector<u8> data;};
std::map<std::string,Node> disks[2];
unsigned writes{};
struct Disk{
 bool usb;u64 gen;Disk(ui::ProgressBox*,bool u,u64 g):usb(u),gen(g){}
 Result Open(){return gen==7?0:Bad;}Result Check(){return 0;}Result Flush(){return 0;}Result Space(u64){return 0;}
 Result Stat(const std::string& p,Info& i){i={};auto it=disks[usb].find(p);if(it!=disks[usb].end())i={true,it->second.dir,it->second.data.size()};return 0;}
 Result List(const std::string& p,std::vector<nexusapp::Item>& out){out.clear();for(const auto& [n,v]:disks[usb])if(n!="/"&&f::Parent(n)==p)out.push_back({f::Leaf(n),n,v.data.size(),v.dir});return 0;}
 Result Read(const std::string& p,u64 off,void* out,size_t n,u64& got){auto it=disks[usb].find(p);if(it==disks[usb].end()||off>it->second.data.size())return Bad;got=std::min<u64>(n,it->second.data.size()-off);memcpy(out,it->second.data.data()+off,got);return 0;}
 Result Create(const std::string& p,u64 size){assert(p.starts_with("/nexus-port-manager/"));if(disks[usb].contains(p))return Bad;disks[usb][p]={false,std::vector<u8>(size)};++writes;return 0;}
 Result Write(const std::string& p,u64 off,const void* data,size_t n){auto& bytes=disks[usb].at(p).data;assert(off+n<=bytes.size());memcpy(bytes.data()+off,data,n);return 0;}
 Result Hash(const std::string& p,u64 size,std::string& h){auto it=disks[usb].find(p);if(it==disks[usb].end()||it->second.data.size()!=size)return Bad;u8 out[32];SHA256(it->second.data.data(),size,out);h=nexusapp::HexId(out);return 0;}
 Result NewText(const std::string& p,const std::string& s){R_TRY(Create(p,s.size()));return Write(p,0,s.data(),s.size());}
};
std::string ModRoot(const Port&){return "/nexus-port-manager/0123456789abcdef";}
std::string Random(){return "0123456789abcdef";}
std::string Encode(const core::ModPlan& p){assert(core::Valid(p));return "validated";}
// PRODUCTION
int main(int argc,char** argv){assert(argc==3);ui::ProgressBox progress;const bool good=std::string(argv[2])=="good";const std::string name=argv[1];
 for(bool target:{false,true}){const std::string root=name=="atmosphere"?"/atmosphere/contents/0100b00b51230000":"/switch/port";const bool expected=good&&!(name=="atmosphere"&&target);disks[0].clear();disks[1].clear();writes=0;disks[target][root]={true,{}};disks[target][root+"/save.dat"]={false,{1,2,3}};
  Port port{"Port","/switch/port/app.nro",target,7,0,{root}};nexusfiles::Selection selected{!target,7,{"example.zip","/example.zip",0,false}};
  if(name=="folder"||name=="atmosphere"){selected.item={"mod","/mod",0,true};disks[!target]["/mod"]={true,{}};disks[!target]["/mod/data.bin"]={false,{4,5,6}};}
  else {std::ifstream file(name,std::ios::binary);assert(file);std::vector<u8> bytes{std::istreambuf_iterator<char>(file),{}};selected.item.size=bytes.size();disks[!target]["/example.zip"]={false,std::move(bytes)};}
  progress={};Mod mod{};std::string detail;const auto rc=ImportMod(&progress,port,root,selected,mod,detail);
  assert((rc==0)==expected);assert(disks[target].at(root+"/save.dat").data==std::vector<u8>({1,2,3}));
  const auto manifest=ModRoot(port)+"/0123456789abcdef/manifest.json";assert(disks[target].contains(manifest)==expected);
  if(expected){assert(mod.files==1&&!mod.active);assert(disks[target].at(ModRoot(port)+"/0123456789abcdef/payload/data.bin").data==std::vector<u8>({4,5,6}));}
 }
 std::cout<<"Production folder/ZIP import passed: "<<argv[2]<<" in both SD/USB directions\n";
}
'''.replace('// PRODUCTION',functions)
with tempfile.TemporaryDirectory(prefix='nexus-port-import-') as tmp:
 out=Path(tmp);(out/'test.cpp').write_text(harness)
 subprocess.run(['g++','-std=c++20','-O2','-UNDEBUG','-fsanitize=undefined','-I'+str(app/'sphaira/include'),str(out/'test.cpp'),'-lminizip','-lz','-lcrypto','-o',str(out/'test')],check=True)
 cases=[]
 for label,name in [('valid','data.bin'),('wrapped','switch/port/data.bin'),('traversal','../bad'),('absolute','/bad'),('wronggame','switch/other/data.bin'),('backslash',r'data\bad'),('symlink','data.bin'),('duplicate','data.bin'),('crc','data.bin')]:
  path=out/(label+'.zip')
  with zipfile.ZipFile(path,'w',zipfile.ZIP_STORED) as z:
   info=zipfile.ZipInfo(name)
   if label=='symlink':info.create_system=3;info.external_attr=0o120777<<16
   z.writestr(info,bytes([4,5,6]))
   if label=='duplicate':z.writestr('DATA.BIN',bytes([4,5,6]))
  if label=='crc':
   data=bytearray(path.read_bytes());pos=data.index(bytes([4,5,6]));data[pos]^=1;path.write_bytes(data)
  cases.append((str(path),'good' if label in ('valid','wrapped') else 'bad'))
 for path,state in [('folder','good'),('atmosphere','good'),*cases]:subprocess.run([str(out/'test'),path,state],check=True)
print('All actual import cases passed; traversal, wrong-root, symlink, duplicate and CRC failures cannot publish a mod.')
