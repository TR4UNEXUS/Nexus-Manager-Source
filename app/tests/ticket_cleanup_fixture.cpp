// Host service adapters; production logic is inserted unchanged by the runner.
#include "nexus/game_cleanup_core.hpp"
#include "nexus/nx_Policy.hpp"
#include "nexus/usb_import_scan_core.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>
using u8=std::uint8_t;using u64=std::uint64_t;using u32=std::uint32_t;using s32=std::int32_t;using s64=std::int64_t;using Result=u32;
constexpr Result Invalid=99,Fault=100,Cancelled=101;
constexpr int Module_Libnx=345,LibnxError_IoError=7;
#define MAKERESULT(a,b) ((a)+((b)<<9))
#define R_TRY(x) {const auto rc_=(x);if(rc_)return rc_;}
#define R_UNLESS(x,r) {if(!(x))return r;}
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
template<class F>struct Exit{F f;~Exit(){f();}};
#define ON_SCOPE_EXIT(...) auto JOIN(exit_,__LINE__)=Exit{[&](){__VA_ARGS__;}}
struct NcmContentId{u8 c[16]{};};using FsRightsId=NcmContentId;
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{},install_type{};};
using NcmContentStorage=nexus::Target;
namespace nexus {
constexpr Result Invalid=::Invalid,Unavailable=102;
static Provider provider=Provider::Present;static bool usb=true,active=false,poll=true;static u64 generation=9;
Provider Detect(Result& r){r=Fault;return provider;}
struct Status{u64 generation{};bool Available()const{return usb;}};
struct Info{bool directory{};u64 size{};};
struct Entry{char name[260]{};bool directory{};u64 size{};};
struct Client{bool opened{};Result Query(Status& s){opened=true;s.generation=generation;return 0;}Result Check(u64 g){Status s;Query(s);return usb&&g==generation?0:Unavailable;}Result Stat(u64,const std::string&,Info&);Result List(u64,const std::string&,std::vector<Entry>&){return Fault;}};
namespace lifecycle{bool CanPoll(){return poll;}}
namespace transfer{enum class Mode{Off,Ftp};Mode Active(){return active?Mode::Ftp:Mode::Off;}}
void Log(const char*,Result,u64=0){}
}
namespace sphaira {
namespace ui{struct ProgressBox{bool cancel{};Result ShouldExitResult(){return cancel?Cancelled:0;}void NewTransfer(const char*){}void UpdateTransfer(u64,u64){} };}
namespace fs{struct FsNativeSd{Result CreateDirectoryRecursively(const char*){return 0;}};}
namespace keys{struct Keys{};Result parse_keys(Keys&,bool){return 0;}}
namespace nca{constexpr u8 ContentType_PublicData=5;
struct Header{FsRightsId rights_id{};u64 size{};u8 content_type{},magic{1};bool IsValid()const{return magic==1;}};
Result DecryptHeader(Header*,keys::Keys&,Header&){return Fault;}}
namespace es{static std::set<std::array<u8,16>> common,personal;static Result error{},delete_error{};static unsigned deletes{};static bool change_count{},leave_ticket{};
Result Initialize(){return error;}void Exit(){}
Result CountCommonTicket(s32* n){*n=common.size();return error;}
Result CountPersonalizedTicket(s32* n){*n=personal.size();return error;}
Result List(const auto& ids,s32* n,FsRightsId* out,s32 count){assert(count==s32(ids.size()));*n=0;for(auto id:ids)memcpy(&out[(*n)++],id.data(),16);if(change_count)--*n;return error;}
Result ListCommonTicket(s32* n,FsRightsId* out,s32 count){return List(common,n,out,count);}
Result ListPersonalizedTicket(s32* n,FsRightsId* out,s32 count){return List(personal,n,out,count);}
Result DeleteTicket(const FsRightsId& rights){++deletes;R_TRY(delete_error);std::array<u8,16> id;memcpy(id.data(),&rights,16);if(!leave_ticket){common.erase(id);personal.erase(id);}return 0;}
}
}
Result splCryptoInitialize(){return 0;}void splCryptoExit(){}
namespace sphaira::nexusapp {
namespace core=nexus::cleanup;using Id=core::Id;using Rights=Id;using Digest=std::array<u8,32>;using nexus::Target;
void CleanupStage(const char*){}
static const char* cleanup_stage="test stage";
constexpr Result MissingUsb=MAKERESULT(140,205);
constexpr size_t MaxFiles=262144;
Id Identity(const NcmContentId& c){Id i;memcpy(i.data(),&c,16);return i;}
NcmContentId Content(const Id& i){NcmContentId c;memcpy(&c,i.data(),16);return c;}
std::string HexId(const void* p){char out[33]{};const auto* b=static_cast<const u8*>(p);for(int i=0;i<16;++i)snprintf(out+i*2,3,"%02x",b[i]);return out;}
struct Game{u64 id{};bool location_checked{};std::string location_hint;};
// API_PRODUCTION
// MODEL_PRODUCTION
static std::array<Inventory,3> stores;
static std::array<std::map<Id,nca::Header>,3> headers;
static std::set<Id> routes;
static Result inventory_error{},read_error{},route_error{};
static unsigned inventories{},header_reads{};static bool change_inventory{},changed_header{};
constexpr u64 App=0x0100123400000000ULL,Other=0x0100222200000000ULL;
Id I(u8 n){Id id{};id[15]=n;return id;}
Rights R(u64 title,u8 generation=1){Rights id{};for(int j=7;j>=0;--j){id[j]=title;title>>=8;}id[15]=generation;return id;}
Result ReadUsbRoutingContent(std::set<Id>& ids){ids=routes;return route_error;}
Result ReadInventory(ui::ProgressBox* p,Target target,Inventory& out){R_TRY(p->ShouldExitResult());R_TRY(inventory_error);out=stores[size_t(target)];++inventories;if(change_inventory&&inventories>3)++out.stamp[0];return 0;}
struct Session{ui::ProgressBox* p;Target storage{},target{};u64 generation{};explicit Session(ui::ProgressBox* x):p(x){}
 Result Open(Target t){target=storage=t;generation=stores[size_t(t)].generation;if(t==Target::Usb&&!nexus::usb)return Fault;return inventory_error;}
 Result Check(){R_TRY(p->ShouldExitResult());return storage==Target::Usb?nexus::Client{}.Check(generation):0;}};
Result FileMatches(Session&,const File&);
Result ReadContent(Target t,Target storage,const NcmContentId& id,u64 generation,u64 offset,void* data,size_t size){
 assert(t==storage&&generation==stores[size_t(t)].generation&&offset==0&&size==sizeof(nca::Header));R_TRY(read_error);++header_reads;auto h=headers[size_t(t)].at(Identity(id));if(changed_header)h.magic=0;memcpy(data,&h,size);return 0;}
Result Database(Session& s,Inventory& out){out.records=stores[size_t(s.storage)].records;return inventory_error;}
Result PhysicalFiles(Session& s,Inventory& out,bool sizes){assert(!sizes);out.files=stores[size_t(s.storage)].files;return inventory_error;}
Result UsbRoutingReferences(const std::set<Id>& ids,bool& found){R_TRY(route_error);found=std::any_of(ids.begin(),ids.end(),[](auto id){return routes.contains(id);});return 0;}
Result OpenStorage(Target t,NcmContentStorage& s){s=t;return inventory_error;}
void ncmContentStorageClose(NcmContentStorage*){}
Result ncmContentStorageHas(NcmContentStorage* s,bool* has,const NcmContentId* id){R_TRY(read_error);*has=stores[size_t(*s)].files.contains(Identity(*id));return 0;}
Result ncmContentStorageGetSizeFromContentId(NcmContentStorage* s,s64* size,const NcmContentId* id){R_TRY(read_error);*size=stores[size_t(*s)].files.at(Identity(*id)).size;return 0;}
Result ncmContentStorageGetContentCount(NcmContentStorage* s,s32* count){*count=stores[size_t(*s)].files.size()+3;return read_error;}
Result ncmContentStorageListContentId(NcmContentStorage* s,NcmContentId* out,s32 capacity,s32* count,s32 offset){
 R_TRY(read_error);*count=0;s32 index=0;for(const auto& [id,f]:stores[size_t(*s)].files)if(index++>=offset&&*count<capacity)out[(*count)++]=f.id;return 0;}
std::string CanonicalUsbContentPath(const NcmContentId& id){return HexId(&id);}
FILE* OpenLog(const char* path,const char*){assert(std::string(path)=="sdmc:/switch/nexusmanager/cleanup-tickets-last.log");return tmpfile();}
std::string TicketName(const Rights&){return "Example game - unused ticket";}
#define fopen OpenLog
// FUNCTIONS_PRODUCTION
#undef fopen
void Add(Target t,u8 id,const Rights& rights){const auto index=size_t(t);const auto key=I(id);stores[index].files.emplace(key,File{Content(key),4096,HexId(key.data())});
 nca::Header h;memcpy(&h.rights_id,rights.data(),16);h.size=4096;headers[index][key]=h;}
void Reset(){stores={};headers={};routes.clear();es::common={R(App),R(Other)};es::personal.clear();es::error=es::delete_error=0;es::deletes=0;es::change_count=es::leave_ticket=false;
 nexus::provider=nexus::Provider::Present;nexus::usb=nexus::poll=true;nexus::active=false;nexus::generation=9;inventory_error=read_error=route_error=0;inventories=header_reads=0;change_inventory=changed_header=false;
 for(size_t i=0;i<3;++i){stores[i].target=Target(i);stores[i].generation=i==2?9:0;}Add(Target::Sd,1,R(App));Add(Target::Usb,2,{});
}
CleanupItem Unused(){CleanupItem item;item.kind=CleanupKind::Ticket;item.target=Target::Internal;item.content=Content(R(Other));return item;}
}
Result nexus::Client::Stat(u64 generation,const std::string& path,Info& info){
 using namespace sphaira::nexusapp;R_UNLESS(opened,Invalid);R_UNLESS(usb&&generation==nexus::generation,Unavailable);R_TRY(read_error);
 for(const auto& [id,f]:stores[2].files)if(CanonicalUsbContentPath(f.id)==path){info.size=f.size;return 0;}return MissingUsb;
}
int main(){using namespace sphaira::nexusapp;using namespace sphaira;ui::ProgressBox p;CleanupScan scan;GameRemovalPlan plan;GameRemovalReport report;
 Reset();assert(!ScanUnusedTickets(&p,scan)&&scan.items.size()==1&&Identity(scan.items[0].content)==R(Other));assert(header_reads==2);
 assert(!PreviewTicketRemoval(&p,Unused(),plan));assert(!ApplyTicketRemoval(&p,plan,report)&&report.complete&&es::deletes==1&&es::common.contains(R(App))&&!es::common.contains(R(Other)));
 // All physical files protect permissions, including USB files without HOME metadata.
 for(auto target:{Target::Sd,Target::Internal,Target::Usb}){Reset();Add(target,3,R(Other));assert(!ScanUnusedTickets(&p,scan)&&scan.items.empty());assert(PreviewTicketRemoval(&p,Unused(),plan));assert(!es::deletes);}
 Reset();es::personal.insert(R(Other));es::common.erase(R(Other));assert(!PreviewTicketRemoval(&p,Unused(),plan));assert(!ApplyTicketRemoval(&p,plan,report)&&!es::personal.contains(R(Other)));
 for(unsigned mode=0;mode<10;++mode){Reset();if(mode==0){nexus::usb=false;routes.insert(I(2));}if(mode==1)routes.insert(I(8));if(mode==2)stores[0].placeholders[I(8)]=200;if(mode==3)read_error=Fault;if(mode==4)changed_header=true;if(mode==5)change_inventory=true;if(mode==6)es::change_count=true;if(mode==7)route_error=Fault;if(mode==8)nexus::active=true;if(mode==9)nexus::generation=10;
  assert(ScanUnusedTickets(&p,scan)&&scan.items.empty()&&!es::deletes);}
 Reset();nexus::usb=false;assert(!ScanUnusedTickets(&p,scan)&&scan.items.size()==1); // no USB routing to account for
 Reset();assert(!PreviewTicketRemoval(&p,Unused(),plan));Add(Target::Usb,5,R(Other));assert(ApplyTicketRemoval(&p,plan,report)&&!es::deletes);
 Reset();assert(!PreviewTicketRemoval(&p,Unused(),plan));++stores[0].stamp[0];assert(ApplyTicketRemoval(&p,plan,report)&&!es::deletes);
 Reset();assert(!PreviewTicketRemoval(&p,Unused(),plan));p.cancel=true;assert(ApplyTicketRemoval(&p,plan,report)&&!es::deletes);p.cancel=false;
 Reset();assert(!PreviewTicketRemoval(&p,Unused(),plan));es::leave_ticket=true;assert(ApplyTicketRemoval(&p,plan,report)&&!report.complete);
 Reset();assert(!PreviewTicketRemoval(&p,Unused(),plan));es::delete_error=Fault;assert(ApplyTicketRemoval(&p,plan,report)==Fault&&es::common.contains(R(Other))&&!report.complete);
 // Missing-ticket families on HOME, zero-rights headers and failed queries.
 Reset();stores[0].references={{App,{I(1)}}};stores[0].home.insert(App);std::set<u64> missing;assert(!MissingTicketGames(&p,stores[0],missing)&&missing.empty());
 es::common.erase(R(App));assert(!MissingTicketGames(&p,stores[0],missing)&&missing==std::set<u64>{App});headers[0][I(1)].rights_id={};assert(!MissingTicketGames(&p,stores[0],missing)&&missing.empty());
 es::error=Fault;assert(MissingTicketGames(&p,stores[0],missing));
 // Location labels include both copies and never mark an inspection complete.
 Reset();Record r;r.application=App;r.ids={I(1),I(2)};stores[0].records={r};stores[2].records={r};std::vector<Game> games{{App,false,{}}};
 assert(!ScanGameLocations(&p,games)&&games[0].location_hint=="SD + USB"&&!games[0].location_checked);
 nexus::usb=false;assert(!ScanGameLocations(&p,games)&&games[0].location_hint=="SD");
 Reset();Add(Target::Internal,3,{});r.ids={I(3)};stores[1].records={r};assert(!ScanGameLocations(&p,games)&&games[0].location_hint=="Internal");
 inventory_error=Fault;assert(!ScanGameLocations(&p,games)&&games[0].location_hint.starts_with("Location check failed: 0x")&&!games[0].location_checked);
 // Production counterpart decisions protect exact copies and offline routes.
 Reset();bool keep{};Record record;record.ids={I(1)};assert(!KeepSharedRecord(&p,Target::Usb,record,keep)&&keep);
 record.ids={I(2)};assert(!KeepSharedRecord(&p,Target::Sd,record,keep)&&keep);
 record.ids={I(4)};assert(!KeepSharedRecord(&p,Target::Sd,record,keep)&&!keep);
 nexus::usb=false;routes={I(4)};assert(!KeepSharedRecord(&p,Target::Sd,record,keep)&&keep);
 nexus::provider=nexus::Provider::Absent;assert(!KeepSharedRecord(&p,Target::Sd,record,keep)&&keep);
 nexus::provider=nexus::Provider::Unknown;assert(KeepSharedRecord(&p,Target::Sd,record,keep));
 Reset();Session session(&p);assert(!session.Open(Target::Sd));const auto f=stores[0].files.at(I(1));assert(RemovedFileAbsent(session,f));stores[0].files.erase(I(1));assert(!RemovedFileAbsent(session,f));
 assert(!session.Open(Target::Usb));auto u=stores[2].files.at(I(2));u.path=CanonicalUsbContentPath(u.id);assert(RemovedFileAbsent(session,u));stores[2].files.erase(I(2));assert(!RemovedFileAbsent(session,u));
 Reset();assert(!session.Open(Target::Sd));Inventory physical;assert(!InventoryPhysicalFiles(session,physical)&&physical.files.size()==1); // 4 filesystem entries, 1 NCA
 assert(!FileMatches(session,stores[0].files.at(I(1))));assert(!session.Open(Target::Usb));auto uf=stores[2].files.at(I(2));uf.path=CanonicalUsbContentPath(uf.id);assert(!FileMatches(session,uf));
 std::cout<<"Production ticket and location tests passed: all storage, missing/unused rights, personal tickets, disconnected/replaced USB, placeholders, corruption, races, cancellation, post-delete confirmation, SD+USB labels\n";
}
