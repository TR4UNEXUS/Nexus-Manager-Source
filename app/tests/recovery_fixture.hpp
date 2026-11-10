// Test the production adapter with memory-backed Horizon APIs. Crypto primitive
// is a deterministic stub; target compilation uses libnx's real SHA-256.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "nexus/index_recovery_core.hpp"
#include "nexus/index_repair_core.hpp"
#include "nexus/nx_Policy.hpp"
#include "nexus/release.hpp"
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;using s32=int32_t;using Result=u32;
#define MAKERESULT(m,d) ((m)+((d)<<9))
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
#define R_TRY(x) do{const auto r_=(x);if(r_)return r_;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return (r);}while(0)
constexpr int FS_MAX_PATH=0x301,FsOpenMode_Read=1,FsOpenMode_Write=2,FsWriteOption_Flush=1,FsReadOption_None=0;
constexpr Result Fault=123,NoSd=MAKERESULT(2,1),NoUsb=MAKERESULT(140,205);
constexpr int NcmStorageId_SdCard=5,NcmContentMetaType_Unknown=0,NcmContentMetaType_Application=0x80,NcmContentMetaType_DataPatch=0x84,NcmContentInstallType_Full=0,NcmContentType_Program=1;
constexpr int NcmContentMetaType_Patch=0x81,NcmContentMetaType_AddOnContent=0x82;
struct NcmDataPatchMetaExtendedHeader{u64 data_id,application_id;u32 required_application_version,extended_data_size;u64 padding;};
constexpr int NEXUS_VSD_ACTIVE=1,NEXUS_VSD_ARMED=2;
enum FsDirEntryType{FsDirEntryType_File,FsDirEntryType_Dir};
struct FsFileSystem{};struct FsFile{std::string path;bool open{};};
struct NcmContentStorage{bool native{};};struct NcmContentMetaDatabase{};
struct NcmContentId{u8 c[16]{};};
struct NcmContentInfo{NcmContentId content_id;u32 size_low;u8 size_high,attr,content_type,id_offset;};
struct NcmContentMetaKey{u64 id;u32 version;u8 type,install_type,padding[2];};
struct NcmContentMetaHeader{u16 extended_header_size,content_count,content_meta_count;u8 attributes,storage_id;};
struct NcmContentMetaInfo{u8 data[16];};
static_assert(sizeof(NcmContentInfo)==24 && sizeof(NcmContentMetaHeader)==8);
struct NexusVsdStatus{u32 flags{};};
using Bytes=std::vector<u8>;
struct Record{NcmContentMetaKey key;Bytes data;};
static std::vector<Record> records;
static std::map<std::string,Bytes> disk,usb_disk;
static std::set<std::string> dirs;
static int handles{},fault{},writes{},commits{},renames{},fail_rename=-1;
static bool duplicate{},cancelled{},vsd{},boot=true,report_fault{};
static int reads{},cancel_after=-1,disconnect_after=-1,short_after=-1;
static std::map<u64,std::string> title_names;
static u64 generation=1;
static constexpr const char* live="/atmosphere/usb_content";
void Reset(){records.clear();disk.clear();usb_disk.clear();dirs.clear();title_names.clear();handles=writes=commits=fault=renames=reads=0;fail_rename=cancel_after=disconnect_after=short_after=-1;duplicate=cancelled=vsd=report_fault=false;boot=true;generation=1;}
std::string NcaPath(u8 id){char p[80];snprintf(p,sizeof(p),"/registered/%02x000000000000000000000000000000.nca",id);return p;}
bool nexusHasSdBootstrap(const char*,size_t){return boot;}
const char* envGetLoaderInfo(){return "";}size_t envGetLoaderInfoSize(){return 0;}
u64 armGetSystemTick(){static u64 tick=42;return tick++;}
void Path(const char* p){assert(p[FS_MAX_PATH-1]==0);assert(strlen(p)<FS_MAX_PATH);}
Result fsOpenSdCardFileSystem(FsFileSystem*){return fault==1?Fault:0;}void fsFsClose(FsFileSystem*){}
Result fsFsGetEntryType(FsFileSystem*,const char* p,FsDirEntryType* t){Path(p);if(fault==2)return Fault;if(dirs.contains(p)){*t=FsDirEntryType_Dir;return 0;}if(disk.contains(p)){*t=FsDirEntryType_File;return 0;}return NoSd;}
Result fsFsCreateDirectory(FsFileSystem*,const char* p){Path(p);if(fault==3 || dirs.contains(p))return Fault;dirs.insert(p);return 0;}
Result fsFsCreateFile(FsFileSystem*,const char* p,size_t size,int){Path(p);if(fault==4 || disk.contains(p) || (report_fault && std::string(p).find("recovery-reports")!=std::string::npos))return Fault;disk[p].resize(size);return 0;}
Result fsFsOpenFile(FsFileSystem*,const char* p,int,FsFile* f){Path(p);if(fault==5)return Fault;if(!disk.contains(p))return NoSd;f->path=p;f->open=true;++handles;return 0;}
void fsFileClose(FsFile* f){assert(f->open);f->open=false;--handles;}
Result fsFileWrite(FsFile* f,u64 off,const void* data,size_t size,int){++writes;if(fault==6)return Fault;auto& b=disk.at(f->path);assert(off+size<=b.size());memcpy(b.data()+off,data,size);return 0;}
Result fsFileGetSize(FsFile* f,s64* out){*out=disk.at(f->path).size();return fault==7?Fault:0;}
Result fsFileRead(FsFile* f,u64 off,void* out,size_t n,int,u64* got){if(fault==8)return Fault;auto& b=disk.at(f->path);*got=std::min<u64>(n,b.size()-off);memcpy(out,b.data()+off,*got);if(fault==9)--*got;if(fault==10 && *got)((u8*)out)[0]^=1;return 0;}
Result fsFsCommit(FsFileSystem*){++commits;return ((fault==11 && commits==1)||(fault==12 && commits==2))?Fault:0;}
Result fsFsRenameFile(FsFileSystem*,const char* from,const char* to){
    Path(from);Path(to);if(renames++==fail_rename || !disk.contains(from) || disk.contains(to))return Fault;
    disk.emplace(to,disk.at(from));disk.erase(from);return 0;
}
Result fsFsRenameDirectory(FsFileSystem*,const char* from,const char* to){Path(from);Path(to);if(fault==13 || dirs.contains(to))return Fault;
    if(fault==14){dirs.insert(to);return Fault;} // competing publisher
    std::map<std::string,Bytes> moved;for(const auto& [p,b]:disk)if(p.starts_with(std::string(from)+"/"))moved[std::string(to)+p.substr(strlen(from))]=b;
    for(auto it=disk.begin();it!=disk.end();)if(it->first.starts_with(std::string(from)+"/"))it=disk.erase(it);else ++it;
    disk.insert(moved.begin(),moved.end());dirs.erase(from);dirs.insert(to);return 0;
}
void ncmContentMetaDatabaseClose(NcmContentMetaDatabase*){}void ncmContentStorageClose(NcmContentStorage*){}void ncmExit(){}
Result ncmInitialize(){return fault==15?Fault:0;}
Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase*,int){return 0;}
Result ncmContentStorageGetPath(NcmContentStorage*,char* p,size_t n,const NcmContentId* id){snprintf(p,n,"@SdCardContent:%s",NcaPath(id->c[0]).c_str());return 0;}
Result ncmContentStorageHas(NcmContentStorage* s,bool* out,const NcmContentId*){assert(s->native);*out=duplicate;return 0;}
void ncmContentInfoSizeToU64(const NcmContentInfo* i,u64* size){*size=i->size_low|((u64)i->size_high<<32);}
Record& Get(const NcmContentMetaKey* k){for(auto& r:records)if(r.key.id==k->id && r.key.type==k->type && r.key.version==k->version)return r;assert(false);return records[0];}
Result ncmContentMetaDatabaseGetSize(NcmContentMetaDatabase*,u64* size,const NcmContentMetaKey* k){if(fault==16)return Fault;*size=Get(k).data.size();return 0;}
Result ncmContentMetaDatabaseGet(NcmContentMetaDatabase*,const NcmContentMetaKey* k,u64* got,void* out,size_t n){auto& b=Get(k).data;assert(n>=b.size());memcpy(out,b.data(),b.size());*got=b.size();return 0;}
Result ncmContentMetaDatabaseList(NcmContentMetaDatabase*,s32* total,s32* written,NcmContentMetaKey* keys,s32 count,int,u64,u64,u64,int){
    *total=records.size();*written=std::min<s32>(count,*total);for(int i=0;i<*written;++i)keys[i]=records[i].key;
    if(fault==17)++*total;
    return 0;
}
struct Sha256Context{std::array<u8,32> value{};bool first=true;};
void sha256ContextCreate(Sha256Context* c){*c={};}
void sha256ContextUpdate(Sha256Context* c,const void* b,size_t n){if(c->first && n>=16)memcpy(c->value.data(),b,16);c->first=false;}
void sha256ContextGetHash(Sha256Context* c,void* out){memcpy(out,c->value.data(),32);if(fault==18)static_cast<u8*>(out)[0]^=1;}
// Service/key failure path; real crypto and converted/body-damage cases are
// separately tested against the actual ContentHash in test_compat.py.
namespace sphaira::keys {struct Keys{u8 header_key[32]{};};Result parse_keys(Keys&,bool){return Fault;}}
namespace sphaira::crypto {void cryptoAes128Xts(const void* in,void* out,const u8*,u64,u64,u64 count,bool){memmove(out,in,count);}}
namespace nexus {
constexpr Result Invalid=99,Unavailable=100;
Result CheckUsbFirmware(){return 0;}
struct Info{u64 size{};u32 cluster{};bool directory{};};
struct Status{u64 generation{};bool Available()const{return true;}};
class Client{
public:
    Result Query(Status& s){s.generation=::generation;return 0;}
    Result VirtualSdStatus(NexusVsdStatus& s){s.flags=vsd?NEXUS_VSD_ACTIVE:0;return 0;}
    Result Check(u64 gen){return gen==::generation && fault!=19?0:Unavailable;}
    Result Stat(u64,const std::string& p,Info& out){if(fault==20)return Fault;if(!usb_disk.contains(p))return NoUsb;out.size=usb_disk.at(p).size();return 0;}
    Result Read(u64,const std::string& p,u64 off,void* out,size_t size,u64& got){auto& b=usb_disk.at(p);assert(off+size<=b.size());memcpy(out,b.data()+off,size);got=size;++reads;if(fault==21 || reads==short_after)--got;if(reads==cancel_after)cancelled=true;if(reads==disconnect_after)++generation;return 0;}
};
}
namespace sphaira::ui{class ProgressBox{public:
    s64 offset{};std::vector<s64> points;std::string note;
    Result ShouldExitResult(){return cancelled?Fault:0;}
    void NewTransfer(const std::string&){}void UpdateTransfer(u64,u64){}
    void SetProgressNote(const std::string& n){note=n;}
    void SetOverallTransfer(s64 o,s64 total,const std::string&,bool=false){
        assert(total==10000 && o>=0 && o<=total);
        // A new scan (0) or explicitly confirmed restore (500 after a prior
        // job) starts a fresh UI. Within a run the percentage cannot retreat.
        if(o==0)points.clear();
        if(!points.empty() && o!=500)assert(o>=points.back());
        points.push_back(o);offset=o;
    }
};}
namespace sphaira::nexusapp{
using nexus::Target;
Result OpenStorage(Target t,NcmContentStorage& cs,u64* =nullptr){cs.native=t==Target::Sd;return 0;}
std::string HexId(const void* p,size_t n=16){std::string s;const auto* b=static_cast<const u8*>(p);for(size_t i=0;i<n;++i){char v[3];snprintf(v,sizeof(v),"%02x",b[i]);s+=v;}return s;}
}
namespace sphaira::title {std::string GetCachedTitleName(u64 id){return title_names[id];}}
