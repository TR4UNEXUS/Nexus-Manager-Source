// Host adapters. The plan, scan and mutation functions below are extracted
// unchanged from production. No console filesystem or network is accessed.
#include "nexus/game_cleanup_core.hpp"
#include "nexus/task_progress.hpp"
#include "nexus/nx_Policy.hpp"
#include "nexus/safety.hpp"
#include "nexus/usb_import_diagnostic.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unistd.h>
#include <openssl/sha.h>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;using Result=u32;using s64=std::int64_t;
constexpr Result Invalid=99,Fault=100,Cancelled=101;
constexpr int Module_Libnx=345,LibnxError_IoError=7;
constexpr int InfoType_ProgramId=18,CUR_PROCESS_HANDLE=0;
static u64 running_id=0x05004e584d470000ULL;
Result svcGetInfo(u64* out,int,int,int){*out=running_id;return 0;}
#define MAKERESULT(a,b) ((a)+((b)<<9))
#define R_TRY(x) {const auto rc_=(x);if(rc_)return rc_;}
#define R_UNLESS(x,r) {if(!(x))return r;}
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
template<class F>struct Exit {F f;~Exit(){f();}};
#define ON_SCOPE_EXIT(...) auto JOIN(exit_,__LINE__)=Exit{[&](){__VA_ARGS__;}}
struct NcmContentId{u8 c[16]{};};using NcmPlaceHolderId=NcmContentId;
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{},install_type{};};
struct NcmContentStorage{nexus::Target target{};};struct NcmContentMetaDatabase{};
namespace sphaira::ncm {struct ContentStorageRecord{NcmContentMetaKey key;u8 storage_id{};};}
namespace sphaira::ui {struct ProgressBox {bool cancel{};Result ShouldExitResult(){return cancel?Cancelled:0;}
    void NewTransfer(const std::string&){}void SetTitle(const std::string&){}void SetProgressNote(const std::string&){}void SetOverallTransfer(u64,u64,const std::string&,bool){}
};struct ProgressScope {ProgressScope(ProgressBox*,unsigned,unsigned){}};}
namespace sphaira::fs {struct FsNativeSd{Result CreateDirectoryRecursively(const char*){return 0;}};}
namespace nexus {
constexpr Result Invalid=::Invalid,Unavailable=102;
int LogicalStorage(Target t){return t==Target::Internal?4:5;}
void Log(const char*,Result,u64=0){}
Result CheckUsbMutationAllowed();
struct Client {Result DeleteRegistered(u64,const std::string&,u64);};
}
namespace sphaira::nexusapp {
namespace core=nexus::cleanup;using Id=core::Id;using Digest=std::array<u8,32>;using nexus::Target;
const char* cleanup_stage="test";void CleanupStage(const char* stage){cleanup_stage=stage;}
std::string CleanupFailure(Result){return "Stopped while checking test storage. No files removed.";}
Id Identity(const NcmContentId& c){Id i;memcpy(i.data(),&c,16);return i;}
NcmContentId Content(const Id& i){NcmContentId c;memcpy(&c,i.data(),16);return c;}
std::string HexId(const void* v,size_t=16){return std::to_string(*static_cast<const u8*>(v));}
struct Game{u64 id{};int badge_state{};};Result UpdateHomeBadge(const Game&){return 0;}
struct Placeholder{Target target{};NcmPlaceHolderId id{};s64 size{};u64 generation{};};
// MODEL_PRODUCTION
// API_PRODUCTION
struct TicketRemovalState{std::array<Digest,3> stamps{};std::array<u64,3> generations{};bool usb{};std::set<Id> used,mapped;};
struct Sha256Context{std::vector<u8> bytes;};
void sha256ContextCreate(Sha256Context* c){c->bytes.clear();}
void sha256ContextUpdate(Sha256Context* c,const void* p,size_t n){const auto* b=static_cast<const u8*>(p);c->bytes.insert(c->bytes.end(),b,b+n);}
void sha256ContextGetHash(Sha256Context* c,void* out){SHA256(c->bytes.data(),c->bytes.size(),static_cast<u8*>(out));}
struct UsbImportScan {
    struct Ref {struct {NcmContentId content_id;} info;};
    struct Row {struct {u64 application_id{};std::vector<Ref> content;} record;std::string name;};
    std::vector<NcmContentId> unresolved_ownership;std::vector<Row> titles;
};
// STATE_PRODUCTION
static Inventory live;
static std::vector<ncm::ContentStorageRecord> home;
static std::set<u64> keep;
static unsigned deletes{},route_calls{},db_removes{},home_changes{},snapshots{};
static Result inventory_error{},preflight_error{},route_error{},gate_error{},home_error{};
static Result placeholder_error{};
static unsigned fail_delete{};static bool change_stamp{},change_generation{};
static std::set<u64> missing_tickets;
static Result ticket_error{},absence_error{};
Result MissingTicketGames(ui::ProgressBox*,const Inventory&,std::set<u64>& out){out=missing_tickets;return ticket_error;}
Result PreviewTicketRemoval(ui::ProgressBox*,const CleanupItem&,GameRemovalPlan&){return Invalid;}
Result ApplyTicketRemoval(ui::ProgressBox*,const GameRemovalPlan&,GameRemovalReport&){return Invalid;}
static constexpr u64 app=0x0100123400000000,other=0x0100123500000000;
Id I(u8 n){Id i{};i[0]=n;return i;}
Result ReadInventory(ui::ProgressBox* p,Target target,Inventory& out,bool placeholders=true){if(placeholders)R_TRY(placeholder_error);R_TRY(p->ShouldExitResult());R_TRY(inventory_error);assert(target==live.target);out=live;++snapshots;
    if(change_stamp)++out.stamp[0];if(change_generation)++out.generation;return 0;}
struct Session {ui::ProgressBox* p;NcmContentStorage storage;NcmContentMetaDatabase db;u64 generation{};
    explicit Session(ui::ProgressBox* x):p(x){}Result Open(Target t,bool placeholders=true){if(placeholders)R_TRY(placeholder_error);storage.target=t;generation=live.generation;return 0;}Result Check(){return p->ShouldExitResult();}};
Result RemovedFileAbsent(Session&,const File& f){R_TRY(absence_error);return live.files.contains(Identity(f.id))?Invalid:0;}
Result FileMatches(Session& s,const File& f){R_TRY(s.Check());R_TRY(preflight_error);const auto it=live.files.find(Identity(f.id));R_UNLESS(it!=live.files.end()&&it->second.size==f.size,Invalid);return 0;}
Result KeepSharedRecord(ui::ProgressBox*,Target,const Record& r,bool& result){result=keep.contains(r.key.id);return 0;}
Result UsbRoutingReferences(const std::set<Id>&,bool& found){found=false;return 0;}
Result RemoveDeletedUsbRouting(ui::ProgressBox*,u64 generation,const std::vector<NcmContentId>& ids,const std::vector<u64>& apps){
    assert(live.target==Target::Usb&&generation==live.generation);++route_calls;
    for(const auto& id:ids)assert(!live.files.contains(Identity(id)));for(auto id:apps)assert(id==app);return route_error;}
Result RemovePlaceholder(const Placeholder& p){assert(live.target==p.target&&live.generation==p.generation);auto i=live.placeholders.find(Identity(p.id));R_UNLESS(i!=live.placeholders.end()&&i->second==u64(p.size),Invalid);live.placeholders.erase(i);++deletes;return 0;}
Result Delete(const NcmContentId& id){++deletes;if(fail_delete&&deletes==fail_delete)return Fault;assert(live.files.erase(Identity(id)));++live.stamp[0];return 0;}
FILE* OpenLog(const char* path,const char* mode){assert((std::string(path)=="sdmc:/switch/nexusmanager/cleanup-last.log"||std::string(path)=="sdmc:/switch/nexusmanager/cleanup-all-last.log")&&std::string(mode)=="wb");return tmpfile();}
void Reset(Target target=Target::Sd){
    missing_tickets.clear();ticket_error=absence_error=placeholder_error=0;
    live={};home.clear();keep.clear();deletes=route_calls=db_removes=home_changes=snapshots=0;inventory_error=preflight_error=route_error=gate_error=home_error=0;fail_delete=0;change_stamp=change_generation=false;
    live.target=target;live.generation=target==Target::Usb?7:0;live.home={app,other};
    for(u8 id=1;id<=5;++id)live.files.emplace(I(id),File{Content(I(id)),u64(id)*100,"/registered/"+std::to_string(id)});
    for(u8 id=1;id<=3;++id){Record r;r.application=app;r.key={app+id,1,u8(0x7f+id),0};r.ids={I(id),I(4)};live.records.push_back(r);home.push_back({r.key,u8(nexus::LogicalStorage(target))});live.references.push_back({app,r.ids});}
    live.references.push_back({other,{I(4)}});live.placeholders.emplace(I(8),800);
}
CleanupItem Item(){CleanupItem i;i.kind=CleanupKind::Game;i.application=app;i.target=live.target;i.generation=live.generation;i.name="Example game";return i;}
}
Result ncmInitialize(){return 0;}void ncmExit(){}
Result ncmContentStorageDelete(NcmContentStorage* s,const NcmContentId* id){assert(s->target!=nexus::Target::Usb);return sphaira::nexusapp::Delete(*id);}
Result ncmContentMetaDatabaseRemove(NcmContentMetaDatabase*,const NcmContentMetaKey* key){using namespace sphaira::nexusapp;++db_removes;std::erase_if(live.records,[&](const auto& r){return nexus::SameMetaKey(r.key,*key);});return 0;}
Result ncmContentMetaDatabaseCommit(NcmContentMetaDatabase*){return 0;}
namespace sphaira::ns {
Result Initialize(){return 0;}void Exit(){}
Result GetApplicationRecords(u64,std::vector<ncm::ContentStorageRecord>& out){out=nexusapp::home;return 0;}
Result DeleteApplicationRecord(u64 id){assert(id==nexusapp::app);++nexusapp::home_changes;R_TRY(nexusapp::home_error);nexusapp::home.clear();return 0;}
Result PushApplicationRecord(u64 id,const ncm::ContentStorageRecord* rows,size_t count){assert(id==nexusapp::app);++nexusapp::home_changes;R_TRY(nexusapp::home_error);nexusapp::home.assign(rows,rows+count);return 0;}
Result InvalidateApplicationControlCache(u64){return 0;}
}
Result nexus::CheckUsbMutationAllowed(){return sphaira::nexusapp::gate_error;}
Result nexus::Client::DeleteRegistered(u64 generation,const std::string& path,u64 size){using namespace sphaira::nexusapp;assert(live.target==Target::Usb&&generation==live.generation);
    for(const auto& [id,f]:live.files)if(f.path==path){assert(f.size==size);return Delete(f.id);}return Invalid;}
namespace sphaira::nexusapp {
#define fopen OpenLog
// FUNCTIONS_PRODUCTION
#undef fopen
}
int main(){using namespace sphaira::nexusapp;sphaira::ui::ProgressBox p;
    for(auto t:{Target::Sd,Target::Internal,Target::Usb}){
        Reset(t);GameRemovalPlan plan;assert(!PreviewGameRemoval(&p,Item(),plan));assert(plan.files==3&&plan.bytes==600&&plan.shared==1&&plan.records==3);
        GameRemovalReport report;assert(!ApplyGameRemoval(&p,plan,report));assert(report.complete&&report.removed==3&&deletes==3&&db_removes==3&&home.empty());
        assert(live.files.size()==2&&live.files.contains(I(4))&&live.files.contains(I(5))&&live.placeholders.contains(I(8)));
        assert(route_calls==(t==Target::Usb?1u:0u));
    }
    // Regression for the reported ownership rejection: a damaged known NCA
    // uses its installed database reference; genuinely unknown metadata blocks.
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        Reset(target);placeholder_error=Fault;GameRemovalPlan plan;GameRemovalReport report;
        assert(!PreviewGameRemoval(&p,Item(),plan)&&!ApplyGameRemoval(&p,plan,report)&&report.complete);
        CleanupScan scan;assert(ScanCleanup(&p,target,scan)==Fault); // full cleanup still requires that inventory
    }
    for(auto target:{Target::Sd,Target::Usb}){
        Reset(target);UsbImportScan scan;scan.unresolved_ownership={Content(I(1)),Content(I(1))};
        MergeUsbOwnership(scan,live);assert(!live.usb_unknown);
        GameRemovalPlan plan;GameRemovalReport report;assert(!PreviewGameRemoval(&p,Item(),plan));
        assert(!ApplyGameRemoval(&p,plan,report)&&report.complete&&report.removed==3);
    }
    {
        Reset(Target::Usb);UsbImportScan scan;scan.unresolved_ownership={Content(I(90))};
        MergeUsbOwnership(scan,live);assert(live.usb_unknown&&live.unresolved_usb.size()==1);
        GameRemovalPlan plan;assert(PreviewGameRemoval(&p,Item(),plan));
        assert(plan.detail.find("1 USB content file")!=std::string::npos&&!deletes&&!db_removes);
    }
    {
        Reset(Target::Usb);GameRemovalPlan plan;GameRemovalReport report;
        assert(!PreviewGameRemoval(&p,Item(),plan));UsbImportScan scan;scan.unresolved_ownership={Content(I(90))};
        MergeUsbOwnership(scan,live);assert(ApplyGameRemoval(&p,plan,report));
        assert(report.detail.find("1 USB content file")!=std::string::npos&&!deletes&&!db_removes&&!home_changes);
    }
    {
        Reset(Target::Usb);UsbImportScan scan;
        UsbImportScan::Row row;row.record.application_id=other;row.record.content.push_back({{Content(I(1))}});
        scan.titles.push_back(row);MergeUsbOwnership(scan,live);
        GameRemovalPlan plan;GameRemovalReport report;assert(!PreviewGameRemoval(&p,Item(),plan));
        assert(plan.shared==2&&!ApplyGameRemoval(&p,plan,report)&&live.files.contains(I(1)));
    }
    // Changed ownership/database, replacement USB, I/O failure or cancellation
    // before the first delete cannot mutate any content or registration.
    for(unsigned fault=0;fault<6;++fault){Reset(Target::Usb);GameRemovalPlan plan;assert(!PreviewGameRemoval(&p,Item(),plan));
        if(fault==0)change_stamp=true;if(fault==1)change_generation=true;if(fault==2)inventory_error=Fault;if(fault==3)preflight_error=Fault;if(fault==4)gate_error=Fault;if(fault==5)p.cancel=true;
        GameRemovalReport report;assert(ApplyGameRemoval(&p,plan,report));assert(!deletes&&!db_removes&&!home_changes&&!route_calls);p.cancel=false;
    }
    Reset();keep.insert(live.records[0].key.id);GameRemovalPlan plan;assert(!PreviewGameRemoval(&p,Item(),plan)&&plan.records==2);GameRemovalReport report;assert(!ApplyGameRemoval(&p,plan,report));assert(home.size()==1&&live.records.size()==1);
    Reset();assert(!PreviewGameRemoval(&p,Item(),plan));fail_delete=2;assert(ApplyGameRemoval(&p,plan,report)==Fault&&report.removed==1&&!report.complete&&!db_removes&&!home_changes);
    fail_delete=0;assert(!PreviewGameRemoval(&p,Item(),plan)&&plan.files==2);assert(!ApplyGameRemoval(&p,plan,report)&&report.complete);
    Reset(Target::Usb);assert(!PreviewGameRemoval(&p,Item(),plan));route_error=Fault;assert(ApplyGameRemoval(&p,plan,report)==Fault&&report.removed==3&&!db_removes&&!home_changes);
    route_error=0;assert(!PreviewGameRemoval(&p,Item(),plan)&&plan.files==0);assert(!ApplyGameRemoval(&p,plan,report)&&db_removes==3);
    Reset();assert(!PreviewGameRemoval(&p,Item(),plan));home_error=Fault;assert(ApplyGameRemoval(&p,plan,report)==Fault&&!db_removes);home_error=0;assert(!PreviewGameRemoval(&p,Item(),plan)&&plan.files==0);assert(!ApplyGameRemoval(&p,plan,report));
    Reset();live.home.erase(app);CleanupScan scan;assert(!ScanCleanup(&p,live.target,scan));assert(scan.items.size()==3); // one family, one orphan, one placeholder
    auto orphan=*std::find_if(scan.items.begin(),scan.items.end(),[](const auto& i){return i.kind==CleanupKind::Content;});assert(!PreviewGameRemoval(&p,orphan,plan)&&plan.files==1);assert(!ApplyGameRemoval(&p,plan,report)&&live.files.contains(I(1))&&!live.files.contains(I(5)));
    Reset(Target::Usb);live.usb_unknown=true;assert(!ScanCleanup(&p,live.target,scan)&&scan.items.size()==1);assert(PreviewGameRemoval(&p,Item(),plan));
    const auto unfinished=scan.items[0];assert(unfinished.kind==CleanupKind::Placeholder&&!PreviewGameRemoval(&p,unfinished,plan));assert(!ApplyGameRemoval(&p,plan,report)&&live.files.size()==5&&live.placeholders.empty());
    Reset();auto protected_app=Item();protected_app.application=0x0100000000000001;assert(PreviewGameRemoval(&p,protected_app,plan));protected_app.application=0x05004e584d470000ULL;assert(PreviewGameRemoval(&p,protected_app,plan));
    running_id=app;assert(PreviewGameRemoval(&p,Item(),plan));running_id=0x05004e584d470000ULL;
    // A ticket lookup cannot make an installed game a cleanup candidate.
    for(auto target:{Target::Sd,Target::Internal,Target::Usb})for(Result error:{Result{0},Fault}){
        Reset(target);missing_tickets.insert(app);ticket_error=error;
        assert(!ScanCleanup(&p,target,scan));
        assert(std::none_of(scan.items.begin(),scan.items.end(),[](const auto& i){return i.application==app;}));
        assert(scan.detail.find("missing-ticket")==std::string::npos);
        CleanupBatchPlan batch;assert(!PreviewCleanupBatch(&p,scan.items,batch));CleanupBatchReport removed;
        assert(!ApplyCleanupBatch(&p,batch,removed));assert(live.files.contains(I(1))&&live.files.contains(I(2))&&live.files.contains(I(3)));
    }
    // A game registered after the cleanup preview must be protected, even if
    // the content files and all content metadata are otherwise unchanged.
    Reset();live.home.erase(app);assert(!ScanCleanup(&p,Target::Sd,scan));
    auto stale=*std::find_if(scan.items.begin(),scan.items.end(),[](const auto& i){return i.application==app;});
    assert(stale.cleanup_only&&!PreviewGameRemoval(&p,stale,plan));live.home.insert(app);
    assert(ApplyGameRemoval(&p,plan,report)&&!deletes&&!db_removes&&!home_changes);
    assert(report.detail.find("now on HOME")!=std::string::npos);
    assert(!PreviewGameRemoval(&p,Item(),plan)); // explicit uninstall is still allowed
    // Deleting one complete duplicate must retain all shared HOME/meta records.
    for(auto target:{Target::Sd,Target::Usb}){Reset(target);for(const auto& r:live.records)keep.insert(r.key.id);
        assert(!PreviewGameRemoval(&p,Item(),plan)&&!plan.records);assert(!ApplyGameRemoval(&p,plan,report));
        assert(home.size()==3&&live.records.size()==3&&!home_changes&&!db_removes&&live.files.contains(I(4)));
    }
    Reset();assert(!PreviewGameRemoval(&p,Item(),plan));absence_error=Fault;assert(ApplyGameRemoval(&p,plan,report)==Fault&&!home_changes&&!db_removes);
    // Delete all executes only the reviewed list, tolerates its own earlier
    // removals, keeps shared NCAs and never grows the approved selection.
    Reset();CleanupItem loose;loose.kind=CleanupKind::Content;loose.target=Target::Sd;loose.content=Content(I(5));loose.bytes=500;
    CleanupItem partial=loose;partial.kind=CleanupKind::Placeholder;partial.content=Content(I(8));partial.bytes=800;
    CleanupBatchPlan batch;CleanupBatchReport batch_report;
    assert(!PreviewCleanupBatch(&p,{Item(),loose,partial},batch)&&batch.ready==3&&batch.files==5);
    assert(!deletes&&!ApplyCleanupBatch(&p,batch,batch_report)&&batch_report.removed==3&&live.files.size()==1&&live.files.contains(I(4))&&live.placeholders.empty());
    Reset();assert(!PreviewCleanupBatch(&p,{loose,loose},batch)&&batch.ready==1);live.files.at(I(5)).size=600;
    assert(!ApplyCleanupBatch(&p,batch,batch_report)&&!deletes&&batch_report.skipped==1);
    Reset();assert(!PreviewCleanupBatch(&p,{Item(),loose},batch));live.references.push_back({other,{I(1)}});
    assert(!ApplyCleanupBatch(&p,batch,batch_report)&&batch_report.skipped==1&&batch_report.removed==1&&live.files.contains(I(1)));
    Reset();assert(!PreviewCleanupBatch(&p,{Item(),loose},batch));fail_delete=1;assert(ApplyCleanupBatch(&p,batch,batch_report)==Fault&&batch_report.failed==1&&live.files.contains(I(5)));
    Reset();assert(!PreviewCleanupBatch(&p,{loose},batch));p.cancel=true;assert(ApplyCleanupBatch(&p,batch,batch_report)==Cancelled&&!deletes);p.cancel=false;
    Reset();auto nonexistent=loose;nonexistent.content=Content(I(99));assert(!PreviewCleanupBatch(&p,{nonexistent,loose},batch)&&batch.ready==1&&batch.skipped==1);
    assert(!ApplyCleanupBatch(&p,batch,batch_report)&&batch_report.skipped==1&&batch_report.removed==1);
    std::cout<<"Production removal tests: SD/internal/USB, shared content/copies, stale inventory/media, cancellation/I/O, partial retry, routing/HOME failure and orphan review passed\n";
}
