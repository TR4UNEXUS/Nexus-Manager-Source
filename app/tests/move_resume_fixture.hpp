// Memory-backed Horizon calls; tests compile the actual resume/removal code.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <openssl/sha.h>
#include "nexus/nx_Policy.hpp"
#include "nexus/installed_move_core.hpp"
#include "nexus/overall_transfer.hpp"
#include "nexus/transfer_report.hpp"
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;using s32=int32_t;using s64=int64_t;using Result=u32;
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return r;}while(0)
#define R_SUCCEED() return 0
#define MAKERESULT(m,d) ((m)+((d)<<9))
template<class F>struct Scope {F f;~Scope(){f();}};
#define CAT2(a,b) a##b
#define CAT(a,b) CAT2(a,b)
#define ON_SCOPE_EXIT(...) Scope CAT(scope_,__LINE__){[&](){__VA_ARGS__;}}
constexpr int FS_MAX_PATH=0x301,NcmContentMetaType_Unknown=0,NcmContentInstallType_Unknown=0,NcmContentMetaType_Application=0x80,NcmContentMetaType_DataPatch=0x84;
struct NcmContentId{u8 value[16]{};};
struct NcmContentInfo{NcmContentId content_id;u32 size_low{};u8 size_high{},attributes{},content_type{},id_offset{};};
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{},install_type{},padding[2]{};};
struct NcmContentMetaHeader{u16 extended_header_size{},content_count{},content_meta_count{};u8 attributes{},storage_id{};};
struct NcmContentMetaInfo{u8 bytes[16]{};};struct NcmApplicationMetaExtendedHeader{u8 bytes[16]{};};
struct NcmContentStorage{int target{};};struct NcmContentMetaDatabase{int slot{};};
using Bytes=std::vector<u8>;using Key=std::pair<u64,u32>;
struct TestMeta{NcmContentMetaKey key;Bytes raw;std::vector<NcmContentInfo> infos;};
static std::map<int,std::map<Key,TestMeta>> databases;
static std::array<std::map<u8,Bytes>,3> files;
static u64 generation=1;static int deletes{},fail_delete{},hashes{},fail_read{},queries{},fail_query{},route_checks{},route_removes{},complete_receipts{};
static bool cancelled{},read_disconnect{},metadata_changed{};
static int event(){return ++queries==fail_query?123:0;}
static u64 armGetSystemTick(){static u64 tick=1;return tick++;}static u64 armTicksToNs(u64 t){return t*1000;}
using Sha256Context=SHA256_CTX;
void sha256CalculateHash(void* out,const void* in,size_t n){SHA256(static_cast<const u8*>(in),n,static_cast<u8*>(out));}
void sha256ContextCreate(Sha256Context* c){SHA256_Init(c);}void sha256ContextUpdate(Sha256Context* c,const void* p,size_t n){SHA256_Update(c,p,n);}void sha256ContextGetHash(Sha256Context* c,void* out){SHA256_Final(static_cast<u8*>(out),c);++hashes;}
void ncmContentInfoSizeToU64(const NcmContentInfo* i,u64* n){*n=i->size_low|(u64(i->size_high)<<32);}
void ncmContentStorageClose(NcmContentStorage*){}void ncmContentMetaDatabaseClose(NcmContentMetaDatabase*){}
Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase* d,int slot){d->slot=slot;return event();}
Result ncmContentMetaDatabaseHas(NcmContentMetaDatabase* d,bool* present,const NcmContentMetaKey* k){*present=databases[d->slot].contains({k->id,k->version});return event();}
Result ncmContentMetaDatabaseGetSize(NcmContentMetaDatabase* d,u64* size,const NcmContentMetaKey* k){R_TRY(event());auto it=databases[d->slot].find({k->id,k->version});R_UNLESS(it!=databases[d->slot].end(),123);*size=it->second.raw.size();return 0;}
Result ncmContentMetaDatabaseGet(NcmContentMetaDatabase* d,const NcmContentMetaKey* k,u64* got,void* out,size_t size){R_TRY(event());const auto& r=databases.at(d->slot).at({k->id,k->version}).raw;*got=std::min(size,r.size());memcpy(out,r.data(),*got);return 0;}
Result ncmContentMetaDatabaseListContentInfo(NcmContentMetaDatabase* d,s32* count,NcmContentInfo* out,size_t capacity,const NcmContentMetaKey* key,s32 offset){R_TRY(event());const auto& v=databases.at(d->slot).at({key->id,key->version}).infos;R_UNLESS(offset>=0&&size_t(offset)<=v.size(),123);*count=std::min(capacity,v.size()-offset);std::copy_n(v.begin()+offset,*count,out);return 0;}
Result ncmContentMetaDatabaseList(NcmContentMetaDatabase* d,s32* total,s32* count,NcmContentMetaKey* out,size_t capacity,int,u64,u64,u64,int){R_TRY(event());*total=databases[d->slot].size();*count=std::min<size_t>(capacity,*total);size_t i{};for(const auto& [k,r]:databases[d->slot]){(void)k;if(i>=size_t(*count))break;out[i++]=r.key;}return 0;}
Result ncmContentMetaDatabaseRemove(NcmContentMetaDatabase* d,const NcmContentMetaKey* k){R_TRY(event());databases[d->slot].erase({k->id,k->version});return 0;}
Result ncmContentMetaDatabaseCommit(NcmContentMetaDatabase*){return event();}
Result ncmContentStorageHas(NcmContentStorage* s,bool* present,const NcmContentId* id){R_TRY(event());*present=files[s->target].contains(id->value[0]);return 0;}
Result ncmContentStorageGetSizeFromContentId(NcmContentStorage* s,s64* size,const NcmContentId* id){R_TRY(event());*size=files[s->target].at(id->value[0]).size();return 0;}
Result ncmContentStorageDelete(NcmContentStorage* s,const NcmContentId* id){R_TRY(event());if(deletes+1==fail_delete)return 123;files[s->target].erase(id->value[0]);++deletes;return 0;}
namespace nexus::reports {inline void QueueFailure(const char*,Result){}}
namespace nexus {
constexpr Result Invalid=99,Unavailable=100;
inline int LogicalStorage(Target t){return t==Target::Internal?1:0;}
bool SameMetaKey(const NcmContentMetaKey& a,const NcmContentMetaKey& b){return a.id==b.id&&a.version==b.version&&a.type==b.type&&a.install_type==b.install_type;}
Result CheckUsbMutationAllowed(){return event();}void Log(const char*,Result,u64=0){}
Result EnsureUsbContentRegistered(u64 gen,const NcmContentId& id,s64 size){R_TRY(event());return gen==generation && files[2].contains(id.value[0]) && files[2].at(id.value[0]).size()==u64(size)?0:Invalid;}
bool RegisteredFilePath(const std::string& p){return p.starts_with("/registered/")&&p.ends_with(".nca");}
struct Info {u64 size{};bool directory{};};
static u8 Id(const std::string& path){const auto pos=path.rfind('/');return std::stoul(path.substr(pos+1,2),nullptr,16);}
class Client{public:
 Result Check(u64 gen){R_TRY(event());return gen==generation?0:Unavailable;}
 Result Stat(u64 gen,const std::string& path,Info& info){R_TRY(Check(gen));auto it=files[2].find(Id(path));if(it==files[2].end())return MAKERESULT(140,205);info.size=it->second.size();return 0;}
 Result Read(u64 gen,const std::string& path,u64 offset,void* data,size_t size,u64& got){R_TRY(Check(gen));if(fail_read)return 123;const auto& bytes=files[2].at(Id(path));if(read_disconnect)++generation;R_UNLESS(offset+size<=bytes.size(),Invalid);memcpy(data,bytes.data()+offset,size);got=size;return 0;}
 Result DeleteRegistered(u64 gen,const std::string& path,u64 size){R_TRY(Check(gen));R_UNLESS(files[2].at(Id(path)).size()==size,Invalid);if(deletes+1==fail_delete)return 123;files[2].erase(Id(path));++deletes;return 0;}
};
namespace diagnostics {struct Operation{explicit Operation(const char*){}};}
}
namespace sphaira::ui {struct ProgressBox {
 u64 last{};bool ShouldExit(){return cancelled;} Result ShouldExitResult(){return cancelled?123:0;}
 void SetOverallTransfer(u64 n,u64 total,const std::string&,bool=false){assert(n>=last&&n<=total);last=n;}
 void NewTransfer(const std::string&){}void SetActionName(const std::string&){}void UpdateTransfer(u64,u64){}void AddTransferBytes(u64){}
};}
namespace sphaira::ncm {
struct ContentStorageRecord{NcmContentMetaKey key{};int storage_id{};};
Result GetContentInfos(NcmContentMetaDatabase* d,const NcmContentMetaKey* k,std::vector<NcmContentInfo>& out){R_TRY(event());out=databases.at(d->slot).at({k->id,k->version}).infos;return 0;}
}
static std::vector<sphaira::ncm::ContentStorageRecord> home;
namespace sphaira::ns {Result Initialize(){return event();}void Exit(){}Result GetApplicationRecords(u64,std::vector<ncm::ContentStorageRecord>& out){R_TRY(event());out=home;return 0;}Result InvalidateApplicationControlCache(u64){return event();}}
namespace sphaira::nexusapp {
using nexus::Target;
std::string HexId(const void* data,size_t size=16){std::string out;for(size_t i=0;i<size;++i){char b[3];snprintf(b,sizeof(b),"%02x",static_cast<const u8*>(data)[i]);out+=b;}return out;}
Result OpenStorage(Target t,NcmContentStorage& s,u64* gen){s.target=int(t);*gen=t==Target::Usb?generation:0;return event();}
Result ReadContent(Target t,NcmContentStorage&,const NcmContentId& id,u64,u64 offset,void* out,size_t size){R_TRY(event());if(fail_read)return 123;auto& b=files[int(t)].at(id.value[0]);R_UNLESS(offset+size<=b.size(),nexus::Invalid);memcpy(out,b.data()+offset,size);return 0;}
Result CheckUsbRecordRouting(ui::ProgressBox*,u64,const NcmContentMetaKey&,const std::vector<NcmContentInfo>&){++route_checks;return event();}
Result RemoveDeletedUsbRouting(ui::ProgressBox*,u64,const std::vector<NcmContentId>& removed,const std::vector<u64>&){R_TRY(event());for(const auto& id:removed)assert(!files[2].contains(id.value[0]));++route_removes;return 0;}
}
