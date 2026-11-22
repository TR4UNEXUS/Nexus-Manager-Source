// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_import_core.hpp"
#include "nexus/usb_import_diagnostic.hpp"
#include "nexus/usb_import_scan_core.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <tuple>
#include <openssl/sha.h>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;using Result=u32;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define R_UNLESS(c,r) do{if(!(c))return (r);}while(0)
#define R_TRY(e) do{const auto result_=(e);if(result_)return result_;}while(0)
template<class F>struct Scope {F action;~Scope(){action();}};
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__LINE__){[&]{__VA_ARGS__;}}
namespace core=nexus::usbimport;
using nexus::repair::Image;using nexus::repair::Snapshot;using nexus::recovery::Bytes;
constexpr Result Injected=0x1759,Cancelled=0x1234;
struct NcmContentMetaKey {u64 id{};u32 version{};u8 type{0x80},install_type{};};
struct FsRightsId {u8 data[16]{};};
namespace yati {struct ExistingUsbRecord {NcmContentMetaKey key;u64 application_id{};Bytes installed_meta=Bytes(8,1);};}
enum class UsbImportStatus {Ready,AlreadyRegistered,Incomplete,TicketMissing};
enum class UsbImportRegistration {Unknown,Usb,Native,Superseded};
struct UsbImportTitle {yati::ExistingUsbRecord record;UsbImportStatus status{UsbImportStatus::Ready};std::string name,reason;std::vector<FsRightsId> archived_tickets;UsbImportRegistration registration{UsbImportRegistration::Usb};bool missing_tickets{};};
struct UsbImportScan {u64 generation{7};std::vector<UsbImportTitle> titles;bool complete{true};};
struct UsbLibrarySummary {size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{},selected_items{};};
struct UsbLibraryPlan {UsbLibrarySummary summary;std::vector<bool> selected;std::vector<std::string> family_reason;};
// PRODUCTION_PLAN
struct UsbImportResult {std::vector<std::string> entries;size_t imported{},skipped{};Result result{};bool restart{},pending{},rolled_back{};std::string detail,report_path;};
struct World {
    std::vector<std::string> trace;
    std::map<std::string,unsigned> calls;
    std::map<std::string,unsigned> fail;
    Snapshot indexes=nexus::repair::Encode({});
    std::map<core::Key,Image> metadata;
    std::map<u64,Image> home;
    std::map<u64,std::vector<NcmContentMetaKey>> home_keys;
    std::map<core::Key,u8> storage;
    std::vector<u64> verified,published;
    core::Journal current;
    bool pending{},cancel{},disconnected{},unrelated_mutation{},change_identity_after_publish{};
    u64 conflict_app{};
    unsigned live_writes{},journal_writes{},rollback_writes{},worker_count{};
    Result Hit(const std::string& name){trace.push_back(name);const auto n=++calls[name];return fail.contains(name)&&fail[name]==n?Injected:0;}
} world;
namespace ui {struct ProgressBox {bool ShouldExit(){return world.cancel;} Result ShouldExitResult(){return world.cancel?Cancelled:0;}};}
namespace nexus::reports {inline void QueueFailure(const char*,Result){}}
namespace nexus {
constexpr Result Invalid=0x1759; // MAKERESULT(Module_Libnx, LibnxError_BadInput).
Result CheckUsbMutationAllowed(){return world.Hit("guard");}
Result CheckUsbFirmware(){return world.Hit("firmware");}
namespace usbimport {struct WorkerScope {WorkerScope(){++world.worker_count;}~WorkerScope(){--world.worker_count;}};}
}
struct SecmonArgs {u64 X[8]{};};
struct IdentityReply {u32 magic{},type{},id{},fs_version{};u64 start_sector{};} identity_reply;
char identity_paths[256]{};
u64 identity_device=123;
std::array<u8,16> identity_cid{1,2,3,4};
unsigned identity_spl_refs{},identity_handles{};
constexpr int SplConfigItem_DeviceId=8;
void svcCallSecureMonitor(SecmonArgs* args){
    assert(args->X[0]==0xF0000404&&args->X[1]==0&&args->X[2]%4096==0);
    std::memcpy(reinterpret_cast<void*>(args->X[2]),identity_paths,sizeof(identity_paths));
    std::memcpy(&args->X[1],&identity_reply,sizeof(identity_reply));args->X[0]=0;
}
Result splInitialize(){++identity_spl_refs;return 0;}
void splExit(){assert(identity_spl_refs);--identity_spl_refs;}
Result splGetConfig(int key,u64* value){assert(key==SplConfigItem_DeviceId);*value=identity_device;return 0;}
struct FsDeviceOperator {bool active{};};
Result fsOpenDeviceOperator(FsDeviceOperator* device){device->active=true;++identity_handles;return 0;}
Result fsDeviceOperatorGetSdCardCid(FsDeviceOperator* device,void* out,size_t buffer_size,std::int64_t size){assert(device->active&&buffer_size==16&&size==16);std::memcpy(out,identity_cid.data(),16);return 0;}
void fsDeviceOperatorClose(FsDeviceOperator* device){assert(device->active&&identity_handles);device->active=false;--identity_handles;}
void sha256CalculateHash(void* out,const void* bytes,size_t size){SHA256(static_cast<const u8*>(bytes),size,static_cast<u8*>(out));}
// PRODUCTION_IDENTITY
core::Key Key(const NcmContentMetaKey& k){return {k.id,k.version,k.type,k.install_type};}
Image HomeImage(std::vector<NcmContentMetaKey> keys){
    if(keys.empty())return {};
    std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return std::tuple(a.id,a.type,a.version)<std::tuple(b.id,b.type,b.version);});
    Bytes bytes;
    for(const auto& k:keys){nexus::recovery::U64(bytes,k.id);nexus::recovery::U32(bytes,k.version);bytes.push_back(k.type);bytes.push_back(k.install_type);bytes.insert(bytes.end(),2,0);bytes.push_back(world.storage.contains(Key(k))?world.storage.at(Key(k)):5);bytes.insert(bytes.end(),7,0);}
    return {true,bytes};
}
struct Disk {
    ui::ProgressBox* progress{};u64 generation{};Result rc{};const char* step="checking the import request";
    bool Ok(Result r){if(r){rc=r;return false;}return true;}
    Result Open(bool services){assert(services);step="opening import services";return world.Hit("open");}
    bool Check(){
        if(!Ok(world.Hit("check"))){step="checking the USB connection";return false;}
        if(progress&&!Ok(progress->ShouldExitResult())){step="checking cancellation";return false;}
        if(generation&&world.disconnected){step="checking the connected USB drive";return Ok(Injected);}
        return true;
    }
    bool Directory(const std::string&){step="opening recovery folders";return Ok(world.Hit("directory"));}
    bool Read(const std::string& path,Image& out){assert(path==core::Pending);if(!Ok(world.Hit("pending.read")))return false;out=world.pending?Image{true,core::Encode(world.current)}:Image{};return true;}
    bool Indexes(Snapshot& out){if(!Ok(world.Hit("indexes.read")))return false;out=world.indexes;return true;}
    bool HomeRead(u64 app,Image& out){if(!Ok(world.Hit("home.read")))return false;out=world.home[app];return true;}
    bool MetaRead(const core::Key& key,Image& out){if(!Ok(world.Hit("meta.read")))return false;out=world.metadata[key];return true;}
    bool SystemMatches(const core::Journal& j){std::array<u8,32> current{};return Ok(world.Hit("rollback.identity"))&&Ok(SystemIdentity(current))&&current==j.system;}
    bool HomeIntermediate(const Image& image,const core::Journal& j){
        // Publication adds only complete records. Accept a prefix of this
        // family's planned HOME image; production's stricter rule has a
        // separate actual-body test in run_usb_import_tests.py.
        return image.exists&&image.data.size()<=j.home_after.data.size()&&std::equal(image.data.begin(),image.data.end(),j.home_after.data.begin());
    }
    bool HomeWrite(u64 app,const Image& image,u8){
        assert(!progress&&!generation);if(!Ok(world.Hit("rollback.home")))return false;
        ++world.rollback_writes;world.home[app]=image;return true;
    }
    bool MetaWrite(const core::Key& key,const Image& image){
        assert(!progress&&!generation);if(!Ok(world.Hit("rollback.meta")))return false;
        ++world.rollback_writes;world.metadata[key]=image;return true;
    }
    bool IndexesRestore(const core::Journal& j){
        assert(!progress&&!generation);if(!Ok(world.Hit("rollback.indexes")))return false;
        ++world.rollback_writes;world.indexes=j.before;return true;
    }
    bool Finish(const core::Journal&,bool success){
        step=success?"finishing the import journal":"finishing rollback";
        if(!Ok(world.Hit(success?"finish.success":"finish.rollback")))return false;
        ++world.journal_writes;world.pending=false;return true;
    }
};
Result Failure(const Disk& d){return R_FAILED(d.rc)?d.rc:nexus::Invalid;}
std::string Label(const UsbImportTitle& row){return std::to_string(row.record.key.id);}
const char* UsbImportStatusText(UsbImportStatus){return "Skipped";}
bool SnapshotFamily(Disk& d,const std::vector<const UsbImportTitle*>& rows,core::Journal& j){
    d.step="checking this system's recovery identity";
    if(!d.Ok(world.Hit("snapshot")))return false;
    j={};j.application=rows.front()->record.application_id;j.nonce=j.application;
    if(!d.Ok(SystemIdentity(j.system)))return false;
    if(j.application==world.conflict_app)return false;
    j.before=world.indexes;j.home_before=world.home[j.application];
    auto keys=world.home_keys[j.application];
    nexus::repair::Indexes after;assert(nexus::repair::Decode(j.before,after));after.titles[j.application]=0;j.after=nexus::repair::Encode(after);
    for(const auto* row:rows){const auto key=Key(row->record.key);j.metadata.push_back({key,world.metadata[key],{true,row->record.installed_meta}});std::erase_if(keys,[&](const auto& existing){return existing.id==key.id&&existing.type==key.type;});keys.push_back(row->record.key);}
    j.home_after=HomeImage(keys);core::Journal decoded;assert(core::Decode(core::Encode(j),decoded));return true;
}
bool Activate(Disk& d,const core::Journal& j){
    d.step="activating the import recovery journal";
    if(!d.Ok(world.Hit("activate.before")))return false;
    ++world.journal_writes;world.current=j;world.pending=true;
    return d.Ok(world.Hit("activate.after"));
}
namespace nexus::repair {
bool Publish(Disk& d,const Journal& j){
    assert(world.pending);if(!d.Ok(world.Hit("indexes.publish.before")))return false;
    ++world.live_writes;world.indexes=j.after;return d.Ok(world.Hit("indexes.publish.after"));
}
}
namespace yati {
Result VerifyExistingUsbRecord(ui::ProgressBox*,u64 generation,const ExistingUsbRecord& record){assert(generation==7&&!world.pending);world.verified.push_back(record.key.id);return world.Hit("verify");}
Result PublishExistingUsbRecord(ui::ProgressBox*,u64 generation,const ExistingUsbRecord& record,u32){
    assert(generation==7&&world.pending);
    R_TRY(world.Hit("publish.before"));
    ++world.live_writes;world.published.push_back(record.key.id);world.metadata[Key(record.key)]={true,record.installed_meta};
    std::erase_if(world.home_keys[record.application_id],[&](const auto& key){return key.id==record.key.id&&key.type==record.key.type;});
    world.home_keys[record.application_id].push_back(record.key);world.home[record.application_id]=HomeImage(world.home_keys[record.application_id]);
    if(world.unrelated_mutation)world.metadata[Key(record.key)].data[0]^=0xff;
    if(world.change_identity_after_publish)++identity_cid[0];
    const auto result=world.Hit("publish.after");
    if(result){world.cancel=true;world.disconnected=true;}
    return result;
}
}
Result ImportUsbTicketPair(u64 generation,const FsRightsId&){assert(generation==7&&world.pending);return world.Hit("ticket");}
namespace ns {Result SetLowestLaunchVersion(u64){assert(!world.pending);return world.Hit("launch.refresh");}}
struct Game {u64 id{};unsigned badge_state{};};
Result QueueHomeRefresh(u64){assert(!world.pending);return world.Hit("badge.queue");}
Result UpdateHomeBadge(const Game&){return world.Hit("badge.update");}
Result FinishHomeRefresh(u64){return world.Hit("badge.finish");}
void SaveReport(Disk&,UsbImportResult& out){world.Hit("report");out.report_path="fixture-report";}
void BackupConsoleRoutingAfterChange(){assert(!world.pending);world.Hit("backup.confirm");}
// PRODUCTION_IMPORT
unsigned cases{},assertions{};
#define REQUIRE(c) do{++assertions;if(!(c)){std::cerr<<"failed line "<<__LINE__<<": " #c "\n";std::abort();}}while(0)
constexpr u64 Identity(u64 value){return 0x0100000000000000ULL|value;}
UsbImportTitle Title(u64 app,u64 id=0,u8 type=0x80,u32 version=1){UsbImportTitle title;title.record.application_id=Identity(app);title.record.key={Identity(id?id:app),version,type,0};return title;}
UsbImportScan Scan(){UsbImportScan scan;scan.titles={Title(100)};return scan;}
void Seed(const UsbImportTitle& row,u8 storage=5){
    world.metadata[Key(row.record.key)]={true,row.record.installed_meta};world.storage[Key(row.record.key)]=storage;
    world.home_keys[row.record.application_id].push_back(row.record.key);world.home[row.record.application_id]=HomeImage(world.home_keys[row.record.application_id]);
}
UsbImportScan Family(){auto scan=Scan();scan.titles.push_back(Title(100,101,0x81));scan.titles.push_back(Title(100,102,0x82));return scan;}
void Reset(){world=World{};identity_reply={};std::memset(identity_paths,0,sizeof(identity_paths));identity_device=123;identity_cid={1,2,3,4};REQUIRE(!identity_spl_refs&&!identity_handles);++cases;}
Result Run(const UsbImportScan& scan,UsbImportResult& out){ui::ProgressBox p;const auto prior=world.calls["backup.confirm"];const auto result=ImportUsbLibrary(&p,scan,out);REQUIRE(world.calls["backup.confirm"]-prior==unsigned(out.imported&&!out.pending));REQUIRE(world.worker_count==0);REQUIRE(!identity_spl_refs&&!identity_handles);REQUIRE(result==out.result);if(result)REQUIRE(!out.detail.empty());return result;}
bool Mentions(const UsbImportResult& out,const std::string& needle){return out.detail.find(needle)!=std::string::npos;}
int main(){
    UsbImportResult out;
    for(const auto& gate:{"open","guard","firmware","check","snapshot","verify","directory","activate.before"}){
        Reset();world.fail[gate]=1;REQUIRE(Run(Scan(),out)==Injected);REQUIRE(world.live_writes==0);REQUIRE(world.rollback_writes==0);REQUIRE(!world.pending&&!out.pending&&!out.restart&&out.imported==0);REQUIRE(Mentions(out,"0x00001759"));
        if(std::string(gate)=="snapshot"){
            REQUIRE(Mentions(out,"recovery identity"));
            REQUIRE(std::none_of(out.entries.begin(),out.entries.end(),[](const auto& entry){return entry.find("current records or routing conflict")!=std::string::npos;}));
            REQUIRE(std::any_of(out.entries.begin(),out.entries.end(),[](const auto& entry){return entry.find("No registration changes were made")!=std::string::npos;}));
        }
        if(std::string(gate)=="verify")REQUIRE(Mentions(out,"content hashes"));
    }
    for(unsigned invalid=0;invalid<3;++invalid){Reset();auto scan=Scan();if(invalid==0)scan.complete=false;if(invalid==1)scan.generation=0;
        if(invalid==2){REQUIRE(ImportUsbLibrary(nullptr,scan,out)==nexus::Invalid);REQUIRE(out.result==nexus::Invalid&&!out.detail.empty());}
        else REQUIRE(Run(scan,out)==nexus::Invalid);
        REQUIRE(world.calls["open"]==0&&world.journal_writes==0&&world.live_writes==0);
    }
    Reset();world.cancel=true;REQUIRE(Run(Scan(),out)==Cancelled);REQUIRE(!out.pending&&world.journal_writes==0&&world.calls["snapshot"]==0);
    Reset();world.disconnected=true;REQUIRE(Run(Scan(),out)==Injected);REQUIRE(!out.pending&&world.journal_writes==0);
    Reset();auto family=Scan();family.titles.push_back(Title(100,101,0x81));world.fail["verify"]=2;
    REQUIRE(Run(family,out)==Injected);REQUIRE(world.calls["verify"]==2&&world.calls["activate.before"]==0&&out.skipped==2&&world.live_writes==0);
    Reset();world.fail["activate.after"]=1;REQUIRE(Run(Scan(),out)==Injected);REQUIRE(out.pending&&out.restart&&world.pending&&world.live_writes==0&&world.rollback_writes==0);
    for(const auto& gate:{"ticket","indexes.publish.before","indexes.publish.after","publish.before","publish.after","finish.success"}){
        Reset();auto scan=Scan();scan.titles[0].archived_tickets.push_back({{1}});world.fail[gate]=1;
        REQUIRE(Run(scan,out)==Injected);REQUIRE(out.rolled_back&&!out.pending&&out.restart&&!world.pending&&out.imported==0);REQUIRE(world.rollback_writes==3);REQUIRE(!world.metadata[Key(scan.titles[0].record.key)].exists&&!world.home[Identity(100)].exists);REQUIRE(world.calls["badge.queue"]==0&&world.calls["launch.refresh"]==0);
        if(std::string(gate)=="publish.after")REQUIRE(Mentions(out,"registering existing USB content"));
    }
    for(const auto& gate:{"pending.read","rollback.identity","rollback.home","rollback.meta","rollback.indexes","finish.rollback"}){
        Reset();world.fail["publish.after"]=1;world.fail[gate]=1;REQUIRE(Run(Scan(),out)==Injected);
        REQUIRE(out.pending&&!out.rolled_back&&out.restart&&world.pending&&out.imported==0);REQUIRE(world.calls["badge.queue"]==0);
    }
    Reset();world.unrelated_mutation=true;world.fail["publish.after"]=1;REQUIRE(Run(Scan(),out)==Injected);REQUIRE(world.rollback_writes==0&&out.pending&&world.pending&&!out.rolled_back);
    Reset();world.change_identity_after_publish=true;world.fail["publish.after"]=1;REQUIRE(Run(Scan(),out)==Injected);REQUIRE(world.rollback_writes==0&&out.pending&&world.pending&&!out.rolled_back);
    for(unsigned malformed=0;malformed<4;++malformed){
        Reset();if(malformed==0)identity_reply.type=1;if(malformed==1)identity_reply.id=5;if(malformed==2)identity_paths[128]='x';if(malformed==3)identity_cid={};
        REQUIRE(Run(Scan(),out)==nexus::Invalid);REQUIRE(world.journal_writes==0&&world.live_writes==0&&world.calls["verify"]==0&&world.calls["publish.before"]==0);REQUIRE(Mentions(out,"recovery identity"));
    }
    Reset();family=Scan();family.titles.push_back(Title(100,101,0x81));world.fail["publish.after"]=1;
    REQUIRE(Run(family,out)==Injected);REQUIRE(out.rolled_back&&!out.pending&&out.skipped==2&&world.rollback_writes==4);
    Reset();auto two=Scan();two.titles.push_back(Title(200));world.fail["publish.after"]=2;
    REQUIRE(Run(two,out)==Injected);REQUIRE(out.imported==1&&out.skipped==1&&out.rolled_back&&!out.pending);REQUIRE(world.home[Identity(100)].exists&&!world.home[Identity(200)].exists);REQUIRE(world.metadata[Key(two.titles[0].record.key)].exists&&!world.metadata[Key(two.titles[1].record.key)].exists);
    Reset();REQUIRE(Run(Scan(),out)==0);REQUIRE(out.imported==1&&out.skipped==0&&out.restart&&!out.pending&&!out.rolled_back);REQUIRE(world.calls["verify"]==1&&world.calls["publish.before"]==1&&world.calls["finish.success"]==1&&world.calls["badge.finish"]==1);
    const auto sysmmc_identity=world.current.system;
    Reset();identity_reply.magic=0x30534645;REQUIRE(Run(Scan(),out)==0);REQUIRE(world.current.system==sysmmc_identity&&out.imported==1);
    Reset();identity_reply.magic=0x30534645;identity_reply.type=1;identity_reply.start_sector=0x12345;REQUIRE(Run(Scan(),out)==0);REQUIRE(world.current.system!=sysmmc_identity&&out.imported==1);
    Reset();family=Scan();family.titles.push_back(Title(100,101,0x81));for(auto& title:family.titles)title.archived_tickets.push_back({{9}});
    REQUIRE(Run(family,out)==0);REQUIRE(out.imported==2&&world.calls["ticket"]==1&&world.calls["verify"]==2&&world.calls["publish.before"]==2);
    Reset();auto skipped=Scan();skipped.titles[0].status=UsbImportStatus::AlreadyRegistered;REQUIRE(Run(skipped,out)==0);REQUIRE(out.imported==0&&out.skipped==1&&!out.restart&&world.journal_writes==0);
    Reset();auto versions=Scan();versions.titles.push_back(Title(100,100,0x80,2));versions.titles.back().status=UsbImportStatus::Incomplete;
    REQUIRE(Run(versions,out)==0);REQUIRE(out.skipped==2&&out.imported==0&&world.calls["snapshot"]==0);
    Reset();world.conflict_app=Identity(100);REQUIRE(Run(Scan(),out)==0);REQUIRE(out.skipped==1&&world.calls["verify"]==0&&world.journal_writes==0);
    for(const auto& cosmetic:{"launch.refresh","badge.queue","badge.update","badge.finish"}){
        Reset();world.fail[cosmetic]=1;REQUIRE(Run(Scan(),out)==0);REQUIRE(out.imported==1&&!out.pending&&world.calls["finish.rollback"]==0&&out.entries.size()==2);
    }
    // The actual planner expands an affected family to healthy USB siblings.
    Reset();family=Family();family.titles[1].status=UsbImportStatus::AlreadyRegistered;Seed(family.titles[1]);
    REQUIRE(Run(family,out)==0);REQUIRE(out.imported==3&&world.verified.size()==3&&world.published.size()==3&&world.current.metadata.size()==3);
    REQUIRE(std::find(world.trace.begin(),world.trace.end(),"activate.before")>std::find(world.trace.begin(),world.trace.end(),"verify"));
    Reset();family=Family();family.titles[1].status=UsbImportStatus::AlreadyRegistered;Seed(family.titles[1]);const auto healthy_home=world.home[Identity(100)];world.fail["verify"]=2;
    REQUIRE(Run(family,out)==Injected);REQUIRE(world.calls["activate.before"]==0&&world.live_writes==0&&out.skipped==3&&world.home[Identity(100)]==healthy_home);
    for(auto status:{UsbImportStatus::Incomplete,UsbImportStatus::TicketMissing}){
        Reset();family=Family();family.titles[2].status=status;family.titles[2].reason="Required family member unavailable";
        REQUIRE(Run(family,out)==0);REQUIRE(out.skipped==3&&out.imported==0&&world.calls["verify"]==0&&world.journal_writes==0&&world.live_writes==0);
        REQUIRE(std::all_of(out.entries.begin(),out.entries.end(),[](const auto& entry){return entry.find("family")!=std::string::npos;}));
        REQUIRE(Mentions(out,"No game families were repaired.")&&Mentions(out,"1 game families were blocked and left unchanged.")&&Mentions(out,"No registration changes were made."));
        REQUIRE(!Mentions(out,"Import finished")&&!Mentions(out,"Repair completed for the repairable"));
    }
    for(auto registration:{UsbImportRegistration::Superseded,UsbImportRegistration::Unknown}){
        Reset();family=Family();family.titles[1].status=UsbImportStatus::AlreadyRegistered;family.titles[1].registration=registration;
        REQUIRE(Run(family,out)==0);REQUIRE(out.skipped==3&&world.calls["verify"]==0&&world.journal_writes==0);
    }
    Reset();family=Family();auto newer=family.titles[1];newer.record.key.version=2;newer.status=UsbImportStatus::Incomplete;family.titles.push_back(newer);
    REQUIRE(Run(family,out)==0);REQUIRE(out.skipped==4&&world.verified.empty()&&world.live_writes==0);
    Reset();family=Family();auto old=family.titles[1];old.record.key.version=0;old.status=UsbImportStatus::Incomplete;family.titles.push_back(old);
    REQUIRE(Run(family,out)==0);REQUIRE(out.imported==3&&out.skipped==1&&world.verified.size()==3&&world.current.metadata.size()==3);
    for(u8 storage:{u8{4},u8{5}})for(bool fail:{false,true}){
        Reset();family=Family();auto& native=family.titles[0];native.status=UsbImportStatus::AlreadyRegistered;native.registration=UsbImportRegistration::Native;native.record.installed_meta=Bytes(8,0x55);Seed(native,storage);
        family.titles[2].status=UsbImportStatus::AlreadyRegistered;Seed(family.titles[2]);const auto before_home=world.home[Identity(100)];const auto native_image=world.metadata[Key(native.record.key)];const auto sibling_image=world.metadata[Key(family.titles[2].record.key)];
        if(fail)world.fail["publish.after"]=2;
        REQUIRE(Run(family,out)==(fail?Injected:0));REQUIRE(world.verified==std::vector<u64>({Identity(101),Identity(102)})&&world.published==world.verified);
        REQUIRE(world.metadata[Key(native.record.key)]==native_image&&world.storage[Key(native.record.key)]==storage&&world.current.metadata.size()==2);
        REQUIRE(world.metadata[Key(family.titles[2].record.key)]==sibling_image);
        if(fail){REQUIRE(out.rolled_back&&!out.pending&&out.imported==0&&world.home[Identity(100)]==before_home&&!world.metadata[Key(family.titles[1].record.key)].exists);}
        else REQUIRE(out.imported==2&&out.skipped==1&&world.home[Identity(100)].data[16]==storage);
    }
    Reset();family=Family();family.titles[0].registration=UsbImportRegistration::Native;
    REQUIRE(Run(family,out)==0);REQUIRE(out.skipped==3&&world.verified.empty()&&world.journal_writes==0);
    Reset();family=Family();family.titles[2].missing_tickets=true;family.titles[2].archived_tickets.push_back({{7}});
    REQUIRE(Run(family,out)==0);REQUIRE(out.imported==3&&world.calls["ticket"]==1&&world.verified.size()==3);
    Reset();family=Family();for(auto& row:family.titles){row.status=UsbImportStatus::AlreadyRegistered;Seed(row);}
    const auto intact_home=world.home[Identity(100)];REQUIRE(Run(family,out)==0);REQUIRE(out.skipped==3&&out.imported==0&&world.verified.empty()&&world.journal_writes==0&&world.home[Identity(100)]==intact_home);
    Reset();family=Family();family.titles[2].status=UsbImportStatus::TicketMissing;family.titles.push_back(Title(200));
    REQUIRE(Run(family,out)==0);REQUIRE(out.imported==1&&out.skipped==3&&world.verified==std::vector<u64>{Identity(200)}&&world.published==world.verified);
    REQUIRE(Mentions(out,"Repair completed for the repairable game families.")&&Mentions(out,"1 game families were blocked and left unchanged."));
    REQUIRE(!Mentions(out,"No game families were repaired.")&&Mentions(out,"Fully restart the Switch before playing."));
    std::cout<<"USB import orchestration: "<<cases<<" cases, "<<assertions<<" assertions passed\n";
}
