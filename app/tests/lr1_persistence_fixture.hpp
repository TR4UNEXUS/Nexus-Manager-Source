// Memory-backed Horizon adapter for the production move checkpoint writer.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <openssl/sha.h>
#include "nexus/nx_Policy.hpp"
#include "nexus/index_repair_core.hpp"
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;using Result=u32;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return (r);}while(0)
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
struct Cleanup{std::function<void()> f;~Cleanup(){f();}};
#define ON_SCOPE_EXIT(...) Cleanup CAT(clean_,__COUNTER__){[&](){__VA_ARGS__;}}
constexpr Result Missing=MAKERESULT(2,1),Fault=123;
constexpr int FS_MAX_PATH=0x301,FsOpenMode_Read=1,FsOpenMode_Write=2,FsWriteOption_Flush=1,FsReadOption_None=0,FsDirOpenMode_ReadFiles=1;
enum FsDirEntryType{FsDirEntryType_Dir,FsDirEntryType_File};
struct NcmContentMetaKey{u64 id;u32 version;u8 type,install_type;u8 pad[2];};
struct NcmContentId{u8 data[16];};
struct NcmContentInfo{NcmContentId content_id;u8 size[5];u8 attr,content_type,id_offset;};
void ncmContentInfoSizeToU64(const NcmContentInfo* p,u64* n){*n=0;memcpy(n,p->size,5);}
struct Service{bool active{};};struct FsFileSystem{Service s;};struct FsFile{std::string path;};
struct FsDir{std::vector<std::string> names;size_t at{};};struct FsDirectoryEntry{char name[FS_MAX_PATH]{};};
std::map<std::string,std::vector<u8>> disk;std::set<std::string> dirs;unsigned calls{},fail_at{};bool short_read{},change_after_save{},wrong_archive{};
bool Op(){return ++calls!=fail_at;}
bool serviceIsActive(Service* s){return s->active;}
void fsFsClose(FsFileSystem* f){f->s.active=false;}void fsFileClose(FsFile*){}void fsDirClose(FsDir*){}
Result fsOpenSdCardFileSystem(FsFileSystem* f){if(!Op())return Fault;f->s.active=true;return 0;}
Result fsFsGetEntryType(FsFileSystem*,const char* p,FsDirEntryType* t){if(!Op())return Fault;if(dirs.contains(p)){*t=FsDirEntryType_Dir;return 0;}if(disk.contains(p)){*t=FsDirEntryType_File;return 0;}return Missing;}
Result fsFsCreateDirectory(FsFileSystem*,const char* p){if(!Op())return Fault;dirs.insert(p);return 0;}
Result fsFsCreateFile(FsFileSystem*,const char* p,s64 n,int){if(!Op())return Fault;if(disk.contains(p))return Fault;disk[p].resize(n);return 0;}
Result fsFsOpenFile(FsFileSystem*,const char* p,int,FsFile* f){if(!Op())return Fault;if(!disk.contains(p))return Missing;f->path=p;return 0;}
Result fsFileGetSize(FsFile* f,s64* n){if(!Op())return Fault;*n=disk.at(f->path).size();return 0;}
Result fsFileRead(FsFile* f,u64 off,void* b,u64 n,int,u64* got){if(!Op())return Fault;auto& data=disk.at(f->path);if(off>data.size())return Fault;*got=std::min<u64>(n,data.size()-off);if(short_read&&*got)--*got;memcpy(b,data.data()+off,*got);return 0;}
Result fsFileWrite(FsFile* f,u64 off,const void* b,u64 n,int){if(!Op())return Fault;auto& data=disk.at(f->path);if(off>data.size()||n>data.size()-off)return Fault;memcpy(data.data()+off,b,n);return 0;}
Result fsFsCommit(FsFileSystem*){return Op()?0:Fault;}
Result fsFsRenameFile(FsFileSystem*,const char* a,const char* b){if(!Op())return Fault;if(!disk.contains(a)||disk.contains(b))return Fault;disk[b]=disk.at(a);disk.erase(a);return 0;}
Result fsFsOpenDirectory(FsFileSystem*,const char* p,int,FsDir* d){if(!Op())return Fault;if(!dirs.contains(p))return Missing;d->names.clear();d->at=0;const std::string prefix=std::string(p)+"/";for(auto& [name,_]:disk)if(name.starts_with(prefix))d->names.push_back(name.substr(prefix.size()));return 0;}
Result fsDirRead(FsDir* d,s64* count,size_t max,FsDirectoryEntry* out){if(!Op())return Fault;*count=std::min(max,d->names.size()-d->at);for(s64 i=0;i<*count;++i)snprintf(out[i].name,sizeof(out[i].name),"%s",d->names[d->at++].c_str());return 0;}
void sha256CalculateHash(void* out,const void* p,size_t n){SHA256(static_cast<const u8*>(p),n,static_cast<u8*>(out));}
u64 tick=1;u64 armGetSystemTick(){return tick++;}const char* envGetLoaderInfo(){return "";}size_t envGetLoaderInfoSize(){return 0;}
bool nexusHasSdBootstrap(const char*,size_t){return true;}constexpr unsigned NEXUS_VSD_ACTIVE=1,NEXUS_VSD_ARMED=2;struct NexusVsdStatus{unsigned flags{};};
namespace nexus{constexpr Result Invalid=321,Unavailable=322;struct Client{Result VirtualSdStatus(NexusVsdStatus&){return 0;}};inline void Log(const char*,Result,u64=0){}}
namespace sphaira::ui{struct ProgressBox{};}
namespace nexus::routing_backup{struct Status{u64 sequence{};};}
namespace sphaira::nexusapp{
nexus::repair::Snapshot archive;Result checkpoint_rc{},archive_rc{};
Result CheckpointConsoleRoutingBackup(){if(checkpoint_rc)return checkpoint_rc;for(unsigned i=0;i<3;++i){archive[i].exists=disk.contains(nexus::repair::Live(i));archive[i].data=disk[nexus::repair::Live(i)];}if(wrong_archive)archive[0].data.push_back(1);if(change_after_save)disk[nexus::repair::Live(0)].push_back(2);return 0;}
Result ReadConsoleRoutingBackup(ui::ProgressBox*,nexus::repair::Snapshot& out,nexus::routing_backup::Status& status){if(archive_rc)return archive_rc;out=archive;status.sequence=42;return 0;}
}
