// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/game_cleanup.hpp"
#include "nexus/game_cleanup_core.hpp"
#include "nexus/usb_import_diagnostic.hpp"
#include "nexus/usb_import_scan.hpp"
#include "nexus/usb_import_scan_core.hpp"
#include "nexus/index_recovery.hpp"
#include "nexus/home_badges.hpp"
#include "nexus/safety.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/nca.hpp"
#include "defines.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <cstdio>
#include <unistd.h>

namespace sphaira::nexusapp {
namespace {
namespace core=nexus::cleanup;
using Id=core::Id;
using Digest=std::array<u8,32>;
constexpr Result Invalid=nexus::Invalid,MissingUsb=MAKERESULT(140,205);
constexpr size_t MaxFiles=262144,MaxReferences=1048576;
thread_local const char* cleanup_stage="starting the storage check";
void CleanupStage(const char* stage){cleanup_stage=stage;}
std::string CleanupFailure(Result rc){
    nexus::Log(cleanup_stage,rc);
    return nexus::usbimport::FailureDetail(cleanup_stage,rc,"No files were removed. The check below could not finish. Details: switch/nexusmanager/nexus.log");
}
Id Identity(const NcmContentId& id){Id out;memcpy(out.data(),&id,16);return out;}
NcmContentId Content(const Id& id){NcmContentId out;memcpy(&out,id.data(),16);return out;}
struct File {NcmContentId id{};u64 size{};std::string path;};
struct Record {NcmContentMetaKey key{};u64 application{};Digest digest{};std::set<Id> ids;};
struct Inventory {
    Target target{};u64 generation{};
    std::vector<Record> records;
    std::vector<core::Reference> references;
    std::map<Id,File> files;
    std::map<u64,std::string> names;
    std::set<u64> home;
    std::map<Id,u64> placeholders;
    Digest stamp{};
    bool usb_unknown{};
    std::set<Id> unresolved_usb;
};
struct Session {
    NcmContentStorage storage{};NcmContentMetaDatabase db{};
    bool ncm{},ns{};u64 generation{};Target target{};ui::ProgressBox* progress;
    explicit Session(ui::ProgressBox* p):progress(p){}
    ~Session(){ncmContentMetaDatabaseClose(&db);ncmContentStorageClose(&storage);if(ns)sphaira::ns::Exit();if(ncm)ncmExit();}
    Result Check(){R_TRY(progress->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        if(target==Target::Usb){nexus::Client c;R_TRY(c.Check(generation));}return 0;}
    Result Open(Target t,bool placeholders=true){
        R_UNLESS(nexus::ValidTarget(t),Invalid);target=t;
        CleanupStage("opening content manager");R_TRY(ncmInitialize());ncm=true;CleanupStage("opening HOME records");R_TRY(sphaira::ns::Initialize());ns=true;
        CleanupStage("opening physical game storage");R_TRY(OpenStorage(t,storage,&generation));
        if(t==Target::Usb && placeholders){
            // Bind placeholder inventory to physical USB, never the routed SD
            // view. The generation-scoped object cannot fall through to SD.
            CleanupStage("opening generation-bound USB storage");ncmContentStorageClose(&storage);storage={};R_TRY(nexus::CheckUsbFirmware());
            R_TRY(serviceDispatchIn(ncmGetServiceSession(),65031,generation,
                .out_num_objects=1,.out_objects=&storage.s));
        }
        CleanupStage("opening game metadata database");R_TRY(ncmOpenContentMetaDatabase(&db,nexus::LogicalStorage(t)));
        return Check();
    }
};
u64 Parent(const NcmContentMetaKey& key,std::span<const u8> raw){
    if(key.type==NcmContentMetaType_Application)return key.id;
    if(key.type!=NcmContentMetaType_Patch&&key.type!=NcmContentMetaType_AddOnContent&&key.type!=NcmContentMetaType_DataPatch)return 0;
    NcmContentMetaHeader header{};if(raw.size()<sizeof(header))return 0;memcpy(&header,raw.data(),sizeof(header));
    const size_t offset=sizeof(header)+(key.type==NcmContentMetaType_DataPatch?8:0);
    if(header.extended_header_size<offset-sizeof(header)+8||raw.size()<offset+8)return 0;
    u64 app{};memcpy(&app,raw.data()+offset,8);return app;
}
Result Database(Session& s,Inventory& out){
    CleanupStage("listing game metadata");s32 total{},count{};
    // Use a real output buffer, including for an empty database. This also
    // avoids depending on firmware handling of a null, zero-size IPC buffer.
    std::vector<NcmContentMetaKey> keys(256);
    R_TRY(ncmContentMetaDatabaseList(&s.db,&total,&count,keys.data(),keys.size(),NcmContentMetaType_Unknown,0,0,UINT64_MAX,NcmContentInstallType_Unknown));
    R_UNLESS(total>=0&&total<=65536&&count>=0&&count<=total&&count<=256,Invalid);
    if(total>static_cast<s32>(keys.size())){
        const auto expected=total;keys.resize(total);
        R_TRY(ncmContentMetaDatabaseList(&s.db,&total,&count,keys.data(),keys.size(),NcmContentMetaType_Unknown,0,0,UINT64_MAX,NcmContentInstallType_Unknown));
        R_UNLESS(total==expected&&count==total,Invalid);
    }else R_UNLESS(count==total,Invalid);
    keys.resize(count);
    std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return std::tie(a.id,a.version,a.type,a.install_type)<std::tie(b.id,b.version,b.type,b.install_type);});
    for(size_t i=1;i<keys.size();++i)R_UNLESS(!nexus::SameMetaKey(keys[i-1],keys[i]),Invalid);
    size_t references{};
    for(const auto& key:keys){
        CleanupStage("reading game metadata and shared-file references");R_TRY(s.Check());u64 size{},actual{};R_TRY(ncmContentMetaDatabaseGetSize(&s.db,&size,&key));
        R_UNLESS(size>=sizeof(NcmContentMetaHeader)&&size<=32*1024*1024,Invalid);
        std::vector<u8> raw(size);R_TRY(ncmContentMetaDatabaseGet(&s.db,&key,&actual,raw.data(),raw.size()));R_UNLESS(actual==size,Invalid);
        NcmContentMetaHeader header{};memcpy(&header,raw.data(),sizeof(header));
        const size_t start=sizeof(header)+header.extended_header_size;
        R_UNLESS(start<=raw.size()&&header.content_count<=(raw.size()-start)/sizeof(NcmContentInfo),Invalid);
        Record record;record.key=key;record.application=Parent(key,raw);sha256CalculateHash(record.digest.data(),raw.data(),raw.size());
        std::vector<NcmContentInfo> infos(header.content_count);s32 read{};
        if(!infos.empty()){
            R_TRY(ncmContentMetaDatabaseListContentInfo(&s.db,&read,infos.data(),infos.size(),&key,0));R_UNLESS(read==header.content_count,Invalid);
            R_UNLESS(!memcmp(infos.data(),raw.data()+start,infos.size()*sizeof(NcmContentInfo)),Invalid);
        }
        for(const auto& info:infos){const auto id=Identity(info.content_id);R_UNLESS(id!=Id{}&&record.ids.insert(id).second,Invalid);}
        references+=record.ids.size();R_UNLESS(references<=MaxReferences,Invalid);
        out.references.push_back({record.application,record.ids});out.records.push_back(std::move(record));
    }
    return s.Check();
}
Result PhysicalFiles(Session& s,Inventory& out,bool sizes=true){
    CleanupStage("listing physical game files");
    if(s.target==Target::Usb){
        nexus::Client client;std::vector<nexus::Entry> buckets;auto rc=client.List(s.generation,"/registered",buckets);
        if(rc==MissingUsb)return 0;R_TRY(rc);R_UNLESS(buckets.size()<=256,Invalid);
        for(const auto& bucket:buckets){
            R_TRY(s.Check());const std::string name(bucket.name,strnlen(bucket.name,sizeof(bucket.name)));
            R_UNLESS(bucket.directory&&name.size()==8&&name.substr(0,6)=="000000"&&nexus::usb_import::Hex(name[6])>=0&&nexus::usb_import::Hex(name[7])>=0,Invalid);
            std::vector<nexus::Entry> entries;R_TRY(client.List(s.generation,"/registered/"+name,entries));
            for(const auto& entry:entries){Id id{};const std::string filename(entry.name,strnlen(entry.name,sizeof(entry.name)));
                // Files without an NCA content ID cannot be referenced by a
                // game. Leave them untouched without blocking game deletion.
                if(!nexus::usb_import::ContentName(filename,id))continue;
                const auto content=Content(id);const auto path=CanonicalUsbContentPath(content);
                const auto listed="/registered/"+name+"/"+filename;
                const auto fold=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;};
                R_UNLESS(!entry.directory&&path.size()==listed.size()&&std::equal(path.begin(),path.end(),listed.begin(),[&](char a,char b){return fold(a)==fold(b);})&&entry.size<=INT64_MAX,Invalid);
                R_UNLESS(out.files.size()<MaxFiles&&out.files.emplace(id,File{content,entry.size,path}).second,Invalid);
            }
        }
    }else{
        s32 total{};R_TRY(ncmContentStorageGetContentCount(&s.storage,&total));R_UNLESS(total>=0&&static_cast<size_t>(total)<=MaxFiles,Invalid);
        for(s32 offset=0;;){
            R_TRY(s.Check());std::array<NcmContentId,256> ids{};s32 read{};
            R_TRY(ncmContentStorageListContentId(&s.storage,ids.data(),ids.size(),&read,offset));
            R_UNLESS(read>=0&&read<=static_cast<s32>(ids.size())&&static_cast<size_t>(offset)+read<=MaxFiles,Invalid);
            if(!read)break; // GetContentCount can also count unrelated files.
            CleanupStage("reading physical content sizes");
            for(s32 i=0;i<read;++i){s64 size{};if(sizes)R_TRY(ncmContentStorageGetSizeFromContentId(&s.storage,&size,&ids[i]));
                R_UNLESS(size>=0&&Identity(ids[i])!=Id{}&&out.files.emplace(Identity(ids[i]),File{ids[i],static_cast<u64>(size),{}}).second,Invalid);}
            offset+=read;
        }
        s32 final{};R_TRY(ncmContentStorageGetContentCount(&s.storage,&final));R_UNLESS(final==total,Invalid);
    }
    return s.Check();
}
void MergeUsbOwnership(const UsbImportScan& scan,Inventory& out){
    // The installed database has already been bounded and read twice via
    // Database/GetContentInfo. Its exact ID references remain authoritative
    // when a known game's on-drive header/CNMT is damaged or legacy-shaped.
    // Unknown metadata from another system still blocks deletion: never guess
    // that its potential shared references are disposable.
    std::set<Id> installed;
    for(const auto& record:out.records)installed.insert(record.ids.begin(),record.ids.end());
    for(const auto& id:scan.unresolved_ownership)if(!installed.contains(Identity(id)))out.unresolved_usb.insert(Identity(id));
    for(const auto& title:scan.titles){
        const auto& record=title.record;
        if(!record.application_id || record.content.empty())continue;
        core::Reference ref;ref.application=record.application_id;
        for(const auto& content:record.content)ref.content.insert(Identity(content.info.content_id));
        out.references.push_back(std::move(ref));
        if(!title.name.empty())out.names.try_emplace(record.application_id,title.name);
    }
    out.usb_unknown=!out.unresolved_usb.empty();
    nexus::Log("cleanup-usb-unresolved-ownership",0,out.unresolved_usb.size());
    nexus::Log("cleanup-usb-database-ownership",0,scan.unresolved_ownership.size()-out.unresolved_usb.size());
}
Result ReadInventory(ui::ProgressBox* p,Target target,Inventory& out,bool placeholders=true){
    out={};out.target=target;Session s(p);R_TRY(s.Open(target,placeholders));out.generation=s.generation;
    p->SetProgressNote("Reading game records and file sizes. Nothing is being deleted.");
    p->SetActionName("Checking installed game records");R_TRY(Database(s,out));
    for(s32 offset=0;;offset+=128){
        R_UNLESS(offset<65536,Invalid);R_TRY(s.Check());std::array<NsApplicationRecord,128> page{};s32 count{};
        R_TRY(nsListApplicationRecord(page.data(),page.size(),offset,&count));R_UNLESS(count>=0&&count<=128,Invalid);
        for(s32 i=0;i<count;++i)out.home.insert(page[i].application_id);if(count<128)break;
    }
    p->SetActionName("Reading physical game files");R_TRY(PhysicalFiles(s,out));
    // Read on-drive CNMTs as well as this system's NCM database. An absent HOME
    // record is not evidence that another emuMMC's USB game is disposable.
    if(target==Target::Usb){
        CleanupStage("reading USB game ownership metadata");UsbImportScan scan;R_TRY(ScanUsbImports(p,scan,s.generation,nullptr,false,true));R_UNLESS(scan.complete,Invalid);
        MergeUsbOwnership(scan,out);
    }
    // Ask the local control-data cache for readable names. Failure to resolve
    // a name does not change ownership or turn an item into an orphan.
    std::set<u64> unnamed;
    for(const auto& ref:out.references)if(core::UserApplication(ref.application)&&!out.names.contains(ref.application))unnamed.insert(ref.application);
    auto control=std::make_unique<NsApplicationControlData>();
    for(auto app:unnamed){R_TRY(s.Check());u64 actual{};memset(control.get(),0,sizeof(*control));
        if(R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,app,control.get(),sizeof(*control),&actual))&&actual>=sizeof(control->nacp)){
            NacpLanguageEntry* entry{};
            if(control->nacp.titles_data_format==0&&R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp,&entry))&&entry&&entry->name[0]){
                const auto length=strnlen(entry->name,sizeof(entry->name));std::string name(entry->name,length);
                if(length<sizeof(entry->name)&&nexus::usb_import::Utf8(name)&&std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32&&c!=127;}))out.names.emplace(app,std::move(name));
            }
        }
    }
    // Installed-game removal does not touch placeholders. A broken or paused
    // unrelated download must not block deleting the selected installed copy.
    if(placeholders){
    CleanupStage("reading unfinished installs");std::array<NcmPlaceHolderId,4096> ph{};s32 count{};
    R_TRY(ncmContentStorageListPlaceHolder(&s.storage,ph.data(),ph.size(),&count));R_UNLESS(count>=0&&count<static_cast<s32>(ph.size()),Invalid);
    for(s32 i=0;i<count;++i){R_TRY(s.Check());s64 size{};R_TRY(ncmContentStorageGetSizeFromPlaceHolderId(&s.storage,&size,&ph[i]));
        Id id;memcpy(id.data(),&ph[i],16);R_UNLESS(size>=0&&out.placeholders.emplace(id,size).second,Invalid);}
    }
    // Digest binds the complete inventory used to prove ownership/shared use.
    Sha256Context hash;sha256ContextCreate(&hash);
    auto add=[&](const void* v,size_t n){sha256ContextUpdate(&hash,v,n);};
    for(const auto& r:out.records){add(&r.key.id,8);add(&r.key.version,4);add(&r.key.type,1);add(&r.key.install_type,1);add(r.digest.data(),r.digest.size());}
    for(const auto& r:out.references){add(&r.application,8);const u64 n=r.content.size();add(&n,8);for(const auto& id:r.content)add(id.data(),id.size());}
    for(const auto& [id,f]:out.files){add(id.data(),id.size());add(&f.size,8);}
    for(const auto& [id,size]:out.placeholders){add(id.data(),id.size());add(&size,8);}
    for(auto app:out.home)add(&app,8);add(&out.usb_unknown,sizeof(out.usb_unknown));
    sha256ContextGetHash(&hash,out.stamp.data());return s.Check();
}
std::set<Id> PhysicalIds(const Inventory& i){std::set<Id> ids;for(const auto& [id,f]:i.files)ids.insert(id);return ids;}
std::string AppName(const Inventory& i,u64 app){const auto name=i.names.find(app);if(name!=i.names.end())return name->second;char b[32];snprintf(b,sizeof(b),"Game %016llX",static_cast<unsigned long long>(app));return b;}
Result FileMatches(Session& s,const File& f){
    R_TRY(s.Check());if(s.target==Target::Usb){nexus::Client client;nexus::Info info{};R_TRY(client.Check(s.generation));R_TRY(client.Stat(s.generation,f.path,info));R_UNLESS(!info.directory&&info.size==f.size,Invalid);}
    else{bool has{};s64 size{};R_TRY(ncmContentStorageHas(&s.storage,&has,&f.id));R_UNLESS(has,Invalid);R_TRY(ncmContentStorageGetSizeFromContentId(&s.storage,&size,&f.id));R_UNLESS(size>=0&&static_cast<u64>(size)==f.size,Invalid);}return 0;
}
// Verify the physical target after deletion; a successful IPC is not itself
// proof that the intended file is gone.
Result RemovedFileAbsent(Session& s,const File& f){
    R_TRY(s.Check());
    if(s.target==Target::Usb){nexus::Client client;nexus::Info info{};R_TRY(client.Check(s.generation));R_UNLESS(client.Stat(s.generation,f.path,info)==MissingUsb,Invalid);}
    else{bool has{};R_TRY(ncmContentStorageHas(&s.storage,&has,&f.id));R_UNLESS(!has,Invalid);}
    return 0;
}
using Rights=std::array<u8,16>;
struct TicketList {
    std::set<Rights> common,personal;
    bool Has(const Rights& id)const{return common.contains(id)||personal.contains(id);}
    bool operator==(const TicketList&)const=default;
};
Result ReadTickets(ui::ProgressBox* p,TicketList& out){
    out={};R_TRY(p->ShouldExitResult());R_TRY(es::Initialize());ON_SCOPE_EXIT(es::Exit());
    for(bool personal:{false,true}){
        s32 count{},written{},after{};R_TRY(personal?es::CountPersonalizedTicket(&count):es::CountCommonTicket(&count));
        R_UNLESS(count>=0&&count<=65536,Invalid);std::vector<FsRightsId> ids(count);
        if(count)R_TRY(personal?es::ListPersonalizedTicket(&written,ids.data(),count):es::ListCommonTicket(&written,ids.data(),count));
        R_TRY(personal?es::CountPersonalizedTicket(&after):es::CountCommonTicket(&after));
        R_UNLESS(written==count&&after==count,Invalid);auto& dest=personal?out.personal:out.common;
        for(const auto& id:ids){Rights value{};memcpy(value.data(),&id,16);R_UNLESS(value!=Rights{}&&dest.insert(value).second,Invalid);}
    }
    return p->ShouldExitResult();
}
// Read the actual NCA header on the chosen physical storage. Some older USB
// NCM rights-query implementations returned zero on path errors; zero from
// that API must never be taken as proof that a ticket is unused.
Result ReadRights(ui::ProgressBox* p,const Inventory& i,std::map<Id,Rights>& rights){
    rights.clear();Session session(p);R_TRY(session.Open(i.target));R_UNLESS(session.generation==i.generation,nexus::Unavailable);
    R_TRY(splCryptoInitialize());ON_SCOPE_EXIT(splCryptoExit());
    keys::Keys keys{};ON_SCOPE_EXIT(keys={});R_TRY(keys::parse_keys(keys,false));
    p->NewTransfer("Checking content permissions");
    for(const auto& [id,file]:i.files){
        R_TRY(FileMatches(session,file));nca::Header header{};R_UNLESS(file.size>=sizeof(header),Invalid);
        R_TRY(ReadContent(i.target,session.storage,file.id,i.generation,0,&header,sizeof(header)));
        if(!header.IsValid())R_TRY(nca::DecryptHeader(&header,keys,header));
        R_UNLESS(header.IsValid()&&header.size==file.size&&header.content_type<=nca::ContentType_PublicData,Invalid);
        Rights value{};memcpy(value.data(),&header.rights_id,16);rights.emplace(id,value);
        R_TRY(FileMatches(session,file));p->UpdateTransfer(rights.size(),i.files.size());
    }
    return session.Check();
}
Result MissingTicketGames(ui::ProgressBox* p,const Inventory& i,std::set<u64>& missing){
    missing.clear();TicketList tickets,after;R_TRY(ReadTickets(p,tickets));std::map<Id,Rights> rights;R_TRY(ReadRights(p,i,rights));
    R_TRY(ReadTickets(p,after));R_UNLESS(tickets==after,Invalid);
    for(const auto& ref:i.references)if(core::UserApplication(ref.application))for(const auto& id:ref.content){
        const auto found=rights.find(id);if(found!=rights.end()&&found->second!=Rights{}&&!tickets.Has(found->second)){missing.insert(ref.application);break;}
    }
    return 0;
}
// The counterpart may share the SD database. Preserve a record whenever any
// physical content survives there; disconnected USB mappings are conservative
// evidence of another copy, not permission to remove that copy's registration.
Result KeepSharedRecord(ui::ProgressBox* p,Target target,const Record& record,bool& keep){
    keep=false;if(target==Target::Internal)return 0;
    if(target==Target::Usb){NcmContentStorage sd{};R_TRY(OpenStorage(Target::Sd,sd));ON_SCOPE_EXIT(ncmContentStorageClose(&sd));
        for(const auto& id:record.ids){R_TRY(p->ShouldExitResult());bool has{};const auto content=Content(id);R_TRY(ncmContentStorageHas(&sd,&has,&content));keep|=has;}return 0;}
    nexus::Client client;nexus::Status status{};Result rc{};const auto provider=nexus::Detect(rc);
    if(provider==nexus::Provider::Unknown)return rc;if(provider==nexus::Provider::Absent)return UsbRoutingReferences(record.ids,keep);
    R_TRY(client.Query(status));
    if(!status.Available()){
        // Read the routing map even with a disconnected USB, without guessing
        // that a temporarily absent drive is an uninstalled game.
        return UsbRoutingReferences(record.ids,keep);
    }
    for(const auto& id:record.ids){R_TRY(p->ShouldExitResult());nexus::Info info{};const auto content=Content(id);const auto r=client.Stat(status.generation,CanonicalUsbContentPath(content),info);
        if(r==MissingUsb)continue;R_TRY(r);R_UNLESS(!info.directory,Invalid);keep=true;}
    return 0;
}
}
struct RemovalState {
    Inventory before;
    std::vector<File> files;
    std::vector<Record> remove_records;
    std::vector<ncm::ContentStorageRecord> home_before,home_after;
    size_t shared{};
};
// Labels are presence hints, separate from the expensive inspection used by
// move/delete. Bulk listing does not decrypt NCAs or read game asset files.
Result ScanGameLocations(ui::ProgressBox* p,std::vector<Game>& games){
    std::map<u64,std::array<bool,3>> locations;std::array<Result,3> errors{};
    std::map<u64,Game> details;std::map<u64,std::set<u64>> dlc;
    std::array<bool,3> checked{};
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        R_TRY(p->ShouldExitResult());
        const auto rc=[&]()->Result{
            Session session(p);R_TRY(session.Open(target));Inventory i;i.target=target;i.generation=session.generation;
            R_TRY(Database(session,i));R_TRY(PhysicalFiles(session,i));
            R_TRY(session.Check());
            const auto slot=static_cast<size_t>(target);checked[slot]=true;
            std::map<u64,std::set<Id>> counted;
            for(const auto& record:i.records){
                auto& game=details[record.application];bool complete=!record.ids.empty();
                for(const auto& id:record.ids){
                    const auto file=i.files.find(id);if(file==i.files.end()){complete=false;continue;}
                    locations[record.application][slot]=true;
                    if(counted[record.application].insert(id).second)game.installed_bytes[slot]+=file->second.size;
                }
                // Do not present an incomplete update as an installed version.
                if(!complete)continue;
                if(record.key.type==NcmContentMetaType_Application){game.has_base=true;game.base_version=std::max(game.base_version,record.key.version);}
                if(record.key.type==NcmContentMetaType_Patch){game.has_update=true;game.update_version=std::max(game.update_version,record.key.version);}
                if(record.key.type==NcmContentMetaType_AddOnContent)dlc[record.application].insert(record.key.id);
            }
            return 0;
        }();
        R_TRY(p->ShouldExitResult());errors[static_cast<size_t>(target)]=rc;
        if(R_FAILED(rc)){nexus::Log(cleanup_stage,rc,static_cast<u64>(target));nexus::Log("game-location-scan",rc,static_cast<u64>(target));}
    }
    for(auto& game:games){
        std::string label;const char* names[]={"SD","Internal","USB"};const auto& present=locations[game.id];
        for(size_t i=0;i<3;++i)if(present[i]){if(!label.empty())label+=" + ";label+=names[i];}
        if(label.empty()){
            const Result failed=errors[0]?errors[0]:errors[1]?errors[1]:errors[2];
            if(failed){char code[48];snprintf(code,sizeof(code),"Location check failed: 0x%08X",unsigned(failed));label=code;}
            else label="No files on connected storage";
        }
        game.location_hint=std::move(label);
        const auto& info=details[game.id];game.installed_bytes=info.installed_bytes;game.size_known=checked;
        game.base_version=info.base_version;game.update_version=info.update_version;
        game.has_base=info.has_base;game.has_update=info.has_update;game.dlc_count=dlc[game.id].size();
    }
    return 0;
}
struct TicketRemovalState {
    TicketList tickets;
    std::set<Rights> used;
    std::set<Id> mapped;
    std::array<Digest,3> stamps{};
    std::array<u64,3> generations{};
    bool usb{};
    bool operator==(const TicketRemovalState&)const=default;
};
namespace {
Result TicketInventory(ui::ProgressBox* p,TicketRemovalState& out,std::string& detail){
    out={};detail="Could not account for all game content. Keep the SD card and USB drive connected, close installers and repair unreadable content before removing tickets.";
    R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
    R_TRY(ReadTickets(p,out.tickets));R_TRY(ReadUsbRoutingContent(out.mapped));
    Result provider_result{};const auto provider=nexus::Detect(provider_result);
    if(provider==nexus::Provider::Unknown)return provider_result;
    if(provider!=nexus::Provider::Absent){nexus::Client client;nexus::Status status{};R_TRY(client.Query(status));out.usb=status.Available();out.generations[2]=out.usb?status.generation:0;}
    if(!out.usb&&!out.mapped.empty()){detail="Reconnect the USB drive used by your games before checking unused tickets. Its saved game mappings are still present, so its tickets will be kept.";return nexus::Unavailable;}
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        if(target==Target::Usb&&!out.usb)continue;
        Inventory i;R_TRY(ReadInventory(p,target,i));const auto index=static_cast<size_t>(target);
        R_UNLESS(!i.usb_unknown,Invalid);
        if(!i.placeholders.empty()){detail="Unfinished installs are present. Finish them, or review and remove the unwanted unfinished installs in Cleanup before checking unused tickets.";return nexus::Unavailable;}
        if(target==Target::Usb){
            R_UNLESS(i.generation==out.generations[2],nexus::Unavailable);
            for(const auto& id:out.mapped)if(!i.files.contains(id)){detail="Some mapped USB game files are missing from this drive. Repair the USB library or reconnect the correct drive before removing tickets.";return nexus::Unavailable;}
        }
        out.stamps[index]=i.stamp;out.generations[index]=i.generation;
        std::map<Id,Rights> rights;R_TRY(ReadRights(p,i,rights));
        for(const auto& [id,value]:rights)if(value!=Rights{})out.used.insert(value);
    }
    TicketList after;R_TRY(ReadTickets(p,after));R_UNLESS(out.tickets==after,Invalid);
    // Detect a download or another installer changing any storage while the
    // header scan was running. An incomplete inventory cannot authorize ES.
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        if(target==Target::Usb&&!out.usb)continue;
        Inventory current;R_TRY(ReadInventory(p,target,current));const auto index=static_cast<size_t>(target);
        R_UNLESS(current.stamp==out.stamps[index]&&current.generation==out.generations[index],Invalid);
    }
    std::set<Id> mapped;R_TRY(ReadUsbRoutingContent(mapped));R_UNLESS(mapped==out.mapped,Invalid);
    if(out.usb){nexus::Client client;R_TRY(client.Check(out.generations[2]));}
    detail.clear();return p->ShouldExitResult();
}
bool UnusedTicket(const TicketRemovalState& state,const Rights& id){
    u64 title{};for(size_t j=0;j<8;++j)title=(title<<8)|id[j];
    return core::UserApplication(title)&&state.tickets.Has(id)&&!state.used.contains(id);
}
std::string TicketName(const Rights& id){
    u64 title{};for(size_t j=0;j<8;++j)title=(title<<8)|id[j];
    char fallback[48];snprintf(fallback,sizeof(fallback),"Unused ticket %016llX",static_cast<unsigned long long>(title));
    if(R_FAILED(ns::Initialize()))return fallback;ON_SCOPE_EXIT(ns::Exit());
    auto control=std::make_unique<NsApplicationControlData>();u64 actual{};
    if(R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage,title,control.get(),sizeof(*control),&actual))||actual<sizeof(control->nacp)||control->nacp.titles_data_format!=0)return fallback;
    NacpLanguageEntry* entry{};
    if(R_FAILED(nacpGetLanguageEntry(&control->nacp,&entry))||!entry)return fallback;
    const auto length=strnlen(entry->name,sizeof(entry->name));std::string name(entry->name,length);
    if(!length||length==sizeof(entry->name)||!nexus::usb_import::Utf8(name)||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))return fallback;
    return name+" - unused ticket";
}
Result PreviewTicketRemoval(ui::ProgressBox* p,const CleanupItem& item,GameRemovalPlan& plan){
    plan={};auto state=std::make_shared<TicketRemovalState>();R_TRY(TicketInventory(p,*state,plan.detail));
    const auto id=Identity(item.content);R_UNLESS(UnusedTicket(*state,id),Invalid);
    plan.item=item;plan.files=1;plan.ticket_state=std::move(state);
    plan.detail="No game file on the checked storage uses this ticket. This removes only this system's permission, not games or saves. It may belong to an uninstalled purchase; keep it if you plan to reinstall or cannot restore it from your original source.";
    return 0;
}
Result ApplyTicketRemoval(ui::ProgressBox* p,const GameRemovalPlan& plan,GameRemovalReport& report){
    report={};report.detail="Ticket removal stopped. No ticket was removed; scan again.";R_UNLESS(plan.ticket_state,Invalid);
    GameRemovalPlan fresh;R_TRY(PreviewTicketRemoval(p,plan.item,fresh));R_UNLESS(*fresh.ticket_state==*plan.ticket_state,Invalid);
    R_TRY(p->ShouldExitResult());R_TRY(es::Initialize());ON_SCOPE_EXIT(es::Exit());
    FsRightsId rights{};memcpy(&rights,&plan.item.content,16);
    R_TRY(fs::FsNativeSd().CreateDirectoryRecursively("/switch/nexusmanager"));
    FILE* log=fopen("sdmc:/switch/nexusmanager/cleanup-tickets-last.log","wb");R_UNLESS(log,MAKERESULT(Module_Libnx,LibnxError_IoError));ON_SCOPE_EXIT(fclose(log));
    R_UNLESS(fprintf(log,"REVIEWED_TICKET %s\n",HexId(&rights).c_str())>=0&&fflush(log)==0&&fsync(fileno(log))==0,MAKERESULT(Module_Libnx,LibnxError_IoError));
    R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
    report.started=true;const auto rc=es::DeleteTicket(rights);
    if(R_FAILED(rc)){report.detail="The system could not remove this ticket. No game files or saves were changed. Scan again before retrying.";return rc;}
    report.removed=1;report.detail="Ticket removal was requested, but the final check did not finish. Scan tickets again. No game files or saves were changed.";
    TicketList remaining;R_TRY(ReadTickets(p,remaining));R_UNLESS(!remaining.Has(Identity(plan.item.content)),Invalid);
    report.complete=true;report.detail="Removed the unused ticket. Game files and saved progress were kept.";
    const bool logged=fprintf(log,"COMPLETE\n")>=0&&fflush(log)==0&&fsync(fileno(log))==0;
    if(!logged)report.detail+=" The report could not be saved.";
    return 0;
}
}
Result ScanUnusedTickets(ui::ProgressBox* p,CleanupScan& scan){
    scan={};TicketRemovalState state;R_TRY(TicketInventory(p,state,scan.detail));
    auto all=state.tickets.common;all.insert(state.tickets.personal.begin(),state.tickets.personal.end());
    for(const auto& id:all)if(UnusedTicket(state,id)){
        R_TRY(p->ShouldExitResult());
        scan.items.push_back({CleanupKind::Ticket,Target::Internal,0,0,0,Content(id),TicketName(id),"No installed content on the checked storage uses this ticket. Review before removing; it may belong to an uninstalled purchase. Rights ID: "+HexId(id.data())});
    }
    scan.detail=std::to_string(scan.items.size())+" unused tickets to review. Tickets used by installed files are kept. Nothing is deleted automatically. Connect any other USB library drives before removing their tickets.";
    return 0;
}
Result ScanCleanup(ui::ProgressBox* p,Target target,CleanupScan& scan){
    scan={};Inventory i;R_TRY(ReadInventory(p,target,i));const auto physical=PhysicalIds(i);
    std::set<u64> missing;const auto ticket_result=MissingTicketGames(p,i,missing);R_TRY(p->ShouldExitResult());
    if(R_FAILED(ticket_result)){missing.clear();nexus::Log("cleanup-ticket-check",ticket_result);}
    std::set<u64> families=missing;
    for(const auto& r:i.references)if(core::UserApplication(r.application)&&!i.home.contains(r.application))families.insert(r.application);
    for(auto app:families){const auto selected=core::Select(app,i.references,physical);u64 size{};for(const auto& id:selected.remove)size+=i.files.at(id).size;
        if(selected.remove.empty()&&std::none_of(i.records.begin(),i.records.end(),[&](const auto& r){return r.application==app;}))continue;
        scan.items.push_back({CleanupKind::Game,target,app,i.generation,size,{},AppName(i,app)+(missing.contains(app)?" - Ticket missing":""),
            missing.contains(app)?"A base game, update or DLC file needs a ticket that is missing in this system. Reinstall from your original backup or use USB repair to keep playing; delete this copy only if unwanted.":"Not on HOME in this system. May belong to another SD card or emuMMC. Review before deleting."});}
    if(!i.usb_unknown)for(const auto& id:core::Unreferenced(i.references,physical)){
        const auto& f=i.files.at(id);scan.items.push_back({CleanupKind::Content,target,0,i.generation,f.size,f.id,"Unreferenced file "+HexId(&f.id),"No game metadata on this storage references this NCA. Select only if you no longer need it."});}
    for(const auto& [id,size]:i.placeholders)scan.items.push_back({CleanupKind::Placeholder,target,0,i.generation,size,Content(id),"Unfinished install "+HexId(id.data()),"Could be a paused download. Close all installers and delete only if the install is no longer wanted."});
    scan.detail=std::to_string(scan.items.size())+" items to review. Nothing selected or deleted automatically. Saves, tickets, mods, homebrew folders and system files are kept.";
    if(R_FAILED(ticket_result))scan.detail+=" Some content permissions could not be checked; missing-ticket games may not be listed. This is not proof that their tickets are present.";
    scan.detail+=" Use Cleanup > Unused tickets to check the system ticket database.";
    if(i.usb_unknown)scan.detail+=" Some USB metadata could not be identified. Game/content deletion is blocked until it can be read; unfinished installs can still be reviewed.";
    return 0;
}
static Result PrepareGameRemoval(ui::ProgressBox* p,const CleanupItem& item,GameRemovalPlan& plan){
    if(item.kind==CleanupKind::Ticket)return PreviewTicketRemoval(p,item,plan);
    plan={};CleanupStage("checking the selected game and storage");R_UNLESS(nexus::ValidTarget(item.target),Invalid);
    if(item.kind==CleanupKind::Game){
        R_UNLESS(core::UserApplication(item.application)&&item.application!=0x05004e584d470000ULL,Invalid);
        u64 running{};R_TRY(svcGetInfo(&running,InfoType_ProgramId,CUR_PROCESS_HANDLE,0));
        // NRO title takeover runs under the launching game's program ID.
        R_UNLESS(item.application!=running,Invalid);
    }
    auto state=std::make_shared<RemovalState>();R_TRY(ReadInventory(p,item.target,state->before,item.kind!=CleanupKind::Game));const auto& i=state->before;
    if(item.generation)R_UNLESS(item.generation==i.generation,nexus::Unavailable);
    std::set<Id> ids;
    if(item.kind==CleanupKind::Placeholder){const auto found=i.placeholders.find(Identity(item.content));R_UNLESS(found!=i.placeholders.end()&&found->second==item.bytes,Invalid);plan.bytes=found->second;plan.files=1;}
    else{
        CleanupStage("checking ownership of USB game files");
        if(i.usb_unknown){
            plan.detail="No files were removed. "+std::to_string(i.unresolved_usb.size())+" USB content file(s) could not be matched to game ownership, including this system's installed-game database. Repair USB library or reinstall the affected content from your original backup, then retry. Details: switch/nexusmanager/nexus.log";
            return Invalid;
        }
        if(item.kind==CleanupKind::Game){auto selection=core::Select(item.application,i.references,PhysicalIds(i));ids=std::move(selection.remove);state->shared=selection.shared.size();}
        else{const auto id=Identity(item.content);R_UNLESS(core::Unreferenced(i.references,PhysicalIds(i)).contains(id),Invalid);R_UNLESS(i.files.at(id).size==item.bytes,Invalid);ids.insert(id);}
        for(const auto& id:ids){state->files.push_back(i.files.at(id));R_UNLESS(plan.bytes<=UINT64_MAX-i.files.at(id).size,Invalid);plan.bytes+=i.files.at(id).size;}
        plan.files=state->files.size();
        if(item.kind==CleanupKind::Game){
            // Read HOME records with NS initialized for this operation.
            CleanupStage("reading this game's HOME records");R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());R_TRY(ns::GetApplicationRecords(item.application,state->home_before));state->home_after=state->home_before;
            R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());
            CleanupStage("protecting copies on other storage");for(const auto& record:i.records)if(record.application==item.application){bool keep{};R_TRY(KeepSharedRecord(p,item.target,record,keep));
                if(!keep){state->remove_records.push_back(record);std::erase_if(state->home_after,[&](const auto& r){return r.storage_id==nexus::LogicalStorage(item.target)&&nexus::SameMetaKey(r.key,record.key);});}}
        }
        CleanupStage("finding this game's files on the chosen storage");R_UNLESS(plan.files||!state->remove_records.empty(),Invalid);
    }
    plan.shared=state->shared;plan.records=state->remove_records.size();plan.item=item;plan.item.generation=i.generation;plan.state=std::move(state);
    plan.detail="Removes only the reviewed game content from "+std::string(nexus::TargetName(item.target))+". Saved progress is kept.";
    if(item.kind==CleanupKind::Game)plan.detail+=" Includes its base game, updates and DLC on this storage. Shared files and copies on other storage are kept.";
    return 0;
}
Result PreviewGameRemoval(ui::ProgressBox* p,const CleanupItem& item,GameRemovalPlan& plan){
    const auto rc=PrepareGameRemoval(p,item,plan);
    if(R_FAILED(rc)){
        if(!plan.detail.empty()){
            nexus::Log(cleanup_stage,rc);
            plan.detail=nexus::usbimport::FailureDetail(item.kind==CleanupKind::Ticket?"checking unused-ticket eligibility":cleanup_stage,rc,plan.detail);
        }
        else plan.detail=CleanupFailure(rc);
        plan.state.reset();plan.ticket_state.reset();
    }
    return rc;
}
Result ApplyGameRemoval(ui::ProgressBox* p,const GameRemovalPlan& plan,GameRemovalReport& report){
    if(plan.item.kind==CleanupKind::Ticket)return ApplyTicketRemoval(p,plan,report);
    report={};report.detail="Deletion stopped before changing files. Scan again and review the selection.";
    R_UNLESS(plan.state,Invalid);const auto& before=*plan.state;
    // Rebuild the entire proof, not just a size check on the selected file.
    GameRemovalPlan fresh;const auto preview_result=PreviewGameRemoval(p,plan.item,fresh);
    if(R_FAILED(preview_result)){report.detail=fresh.detail;return preview_result;}const auto& now=*fresh.state;
    R_UNLESS(before.before.stamp==now.before.stamp&&before.before.generation==now.before.generation&&plan.bytes==fresh.bytes&&plan.files==fresh.files&&plan.shared==fresh.shared&&plan.records==fresh.records,Invalid);
    const auto same_home=[](const auto& a,const auto& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.storage_id==y.storage_id&&nexus::SameMetaKey(x.key,y.key);});};
    R_UNLESS(same_home(before.home_before,now.home_before)&&same_home(before.home_after,now.home_after),Invalid);
    Session session(p);R_TRY(session.Open(plan.item.target,plan.item.kind==CleanupKind::Placeholder));R_UNLESS(session.generation==plan.item.generation,Invalid);
    if(plan.item.target==Target::Usb){R_TRY(nexus::CheckUsbMutationAllowed());bool unused{};R_TRY(UsbRoutingReferences({},unused));}
    for(const auto& file:now.files)R_TRY(FileMatches(session,file));
    R_TRY(fs::FsNativeSd().CreateDirectoryRecursively("/switch/nexusmanager"));
    FILE* log=fopen("sdmc:/switch/nexusmanager/cleanup-last.log","wb");R_UNLESS(log,MAKERESULT(Module_Libnx,LibnxError_IoError));ON_SCOPE_EXIT(fclose(log));
    auto logline=[&](const char* stage,const std::string& text)->Result{R_UNLESS(fprintf(log,"%s %s\n",stage,text.c_str())>=0&&fflush(log)==0&&fsync(fileno(log))==0,MAKERESULT(Module_Libnx,LibnxError_IoError));return 0;};
    R_TRY(logline("REVIEWED",std::string(nexus::TargetName(plan.item.target))+" "+std::to_string(plan.item.application)+" files="+std::to_string(plan.files)));
    for(const auto& file:now.files)R_TRY(logline("PLAN",HexId(&file.id)+" bytes="+std::to_string(file.size)));
    const auto perform=[&]()->Result{
        R_TRY(session.Check());p->SetProgressNote("Removing reviewed files. Saved progress is kept.");
        report.started=true;report.detail="Deletion stopped part way through. Removed files cannot be restored. Run Cleanup or Delete game again to review what remains.";
        if(plan.item.kind==CleanupKind::Placeholder){Placeholder ph{};ph.target=plan.item.target;ph.generation=plan.item.generation;ph.size=plan.bytes;memcpy(&ph.id,&plan.item.content,16);R_TRY(RemovePlaceholder(ph));report.removed=1;report.bytes=plan.bytes;R_TRY(logline("REMOVED_PLACEHOLDER",HexId(&ph.id)));}
        else for(const auto& file:now.files){
            R_TRY(FileMatches(session,file));R_TRY(logline("DELETE",HexId(&file.id)));
            if(plan.item.target==Target::Usb){nexus::Client c;R_TRY(c.DeleteRegistered(session.generation,file.path,file.size));}
            else R_TRY(ncmContentStorageDelete(&session.storage,&file.id));
            R_TRY(RemovedFileAbsent(session,file));
            ++report.removed;report.bytes+=file.size;R_TRY(logline("REMOVED",HexId(&file.id)));
            p->SetOverallTransfer(report.removed,std::max<size_t>(1,plan.files),"Removing reviewed game files",true);
        }
        // Keep the identifying metadata until routing and HOME finish. A
        // stopped operation can be retried even after all NCAs were removed.
        if(plan.item.target==Target::Usb&&plan.item.kind!=CleanupKind::Placeholder){
            std::set<Id> missing;for(const auto& f:now.files)missing.insert(Identity(f.id));
            if(plan.item.kind==CleanupKind::Game)for(const auto& r:now.before.references)if(r.application==plan.item.application)
                for(const auto& id:r.content)if(!now.before.files.contains(id))missing.insert(id);
            std::vector<NcmContentId> removed;for(const auto& id:missing)removed.push_back(Content(id));
            std::vector<u64> titles;if(plan.item.kind==CleanupKind::Game)titles.push_back(plan.item.application);
            report.restart=true;R_TRY(RemoveDeletedUsbRouting(p,session.generation,removed,titles));
        }
        if(plan.item.kind==CleanupKind::Game&&!same_home(now.home_before,now.home_after)){
            R_TRY(session.Check());
            if(now.home_after.empty()){R_TRY(ns::DeleteApplicationRecord(plan.item.application));}
            else {R_TRY(ns::PushApplicationRecord(plan.item.application,now.home_after.data(),now.home_after.size()));}
        }
        for(const auto& record:now.remove_records){R_TRY(session.Check());R_TRY(ncmContentMetaDatabaseRemove(&session.db,&record.key));}
        if(!now.remove_records.empty())R_TRY(ncmContentMetaDatabaseCommit(&session.db));
        if(plan.item.kind==CleanupKind::Game){
            // Cosmetic refresh must not turn a completed deletion into a retry.
            Game game;game.id=plan.item.application;game.badge_state=0;
            if(plan.item.target==Target::Usb||now.home_after.empty())nexus::Log("cleanup-badge",UpdateHomeBadge(game),game.id);
            nexus::Log("cleanup-home-refresh",ns::InvalidateApplicationControlCache(game.id),game.id);
        }
        R_TRY(logline("COMPLETE",std::to_string(report.removed)+" files"));return 0;
    };
    const auto rc=perform();report.complete=R_SUCCEEDED(rc);
    if(report.complete)report.detail="Removed "+std::to_string(report.removed)+" file(s). Saved progress was kept. "+std::to_string(plan.shared)+" shared file(s) kept.";
    else{(void)logline("STOPPED",std::to_string(rc));report.detail+=" Removed so far: "+std::to_string(report.removed)+" file(s).";}
    if(report.restart)report.detail+=" Fully restart the Switch before launching or reinstalling USB games.";
    report.detail+=" Report: switch/nexusmanager/cleanup-last.log";return rc;
}

namespace {
Digest RemovalFingerprint(const GameRemovalPlan& plan){
    Sha256Context hash;sha256ContextCreate(&hash);
    const auto add=[&](const void* data,size_t size){sha256ContextUpdate(&hash,data,size);};
    const auto number=[&](u64 value){add(&value,sizeof(value));};
    number(static_cast<u64>(plan.item.kind));number(static_cast<u64>(plan.item.target));number(plan.item.application);number(plan.item.generation);
    add(&plan.item.content,sizeof(plan.item.content));number(plan.files);number(plan.bytes);number(plan.shared);number(plan.records);
    if(plan.state){
        for(const auto& file:plan.state->files){add(&file.id,16);number(file.size);}
        for(const auto& record:plan.state->remove_records){number(record.key.id);number(record.key.version);number(record.key.type);number(record.key.install_type);add(record.digest.data(),record.digest.size());}
        // Other titles may have been removed earlier in this same batch. This
        // title's files, metadata and surviving HOME registrations must match.
        number(plan.state->home_after.size());
        for(const auto& record:plan.state->home_after){number(record.storage_id);number(record.key.id);number(record.key.version);number(record.key.type);number(record.key.install_type);}
    }
    if(plan.ticket_state){
        const auto& state=*plan.ticket_state;
        // Removing another reviewed unused ticket is expected; changing game
        // content, required rights or the mapped USB drive is not.
        for(const auto& stamp:state.stamps)add(stamp.data(),stamp.size());
        for(auto generation:state.generations)number(generation);
        number(state.usb);number(state.used.size());for(const auto& rights:state.used)add(rights.data(),rights.size());
        number(state.mapped.size());for(const auto& id:state.mapped)add(id.data(),id.size());
    }
    Digest result{};sha256ContextGetHash(&hash,result.data());return result;
}
}
Result PreviewCleanupBatch(ui::ProgressBox* p,const std::vector<CleanupItem>& items,CleanupBatchPlan& batch){
    batch={};R_UNLESS(!items.empty()&&items.size()<=65536,Invalid);
    batch.tickets=items.front().kind==CleanupKind::Ticket;
    std::set<std::tuple<int,int,u64,Id>> seen;
    for(size_t index=0;index<items.size();++index){
        R_TRY(p->ShouldExitResult());const auto& item=items[index];
        R_UNLESS((item.kind==CleanupKind::Ticket)==batch.tickets&&item.target==items.front().target,Invalid);
        const auto key=std::make_tuple(int(item.kind),int(item.target),item.application,Identity(item.content));
        if(!seen.insert(key).second)continue;
        p->SetOverallTransfer(index,items.size(),"Reviewing cleanup list",false);
        p->SetTitle("Review "+std::to_string(index+1)+" / "+std::to_string(items.size()));
        GameRemovalPlan plan;CleanupBatchEntry entry;entry.item=item;
        entry.result=PreviewGameRemoval(p,item,plan);R_TRY(p->ShouldExitResult());
        entry.ready=R_SUCCEEDED(entry.result);entry.detail=plan.detail;
        if(entry.ready){
            entry.item=plan.item;entry.files=plan.files;entry.bytes=plan.bytes;entry.selection=RemovalFingerprint(plan);
            R_UNLESS(batch.bytes<=UINT64_MAX-plan.bytes&&batch.files<=SIZE_MAX-plan.files,Invalid);
            batch.bytes+=plan.bytes;batch.files+=plan.files;++batch.ready;
        }else ++batch.skipped;
        batch.entries.push_back(std::move(entry));
    }
    batch.complete=true;return 0;
}
Result ApplyCleanupBatch(ui::ProgressBox* p,const CleanupBatchPlan& batch,CleanupBatchReport& report){
    report={};report.detail="Bulk cleanup stopped before removing files.";
    R_UNLESS(batch.complete&&batch.ready,Invalid);
    R_TRY(fs::FsNativeSd().CreateDirectoryRecursively("/switch/nexusmanager"));
    FILE* log=fopen("sdmc:/switch/nexusmanager/cleanup-all-last.log","wb");R_UNLESS(log,MAKERESULT(Module_Libnx,LibnxError_IoError));ON_SCOPE_EXIT(fclose(log));
    const auto line=[&](const char* status,const CleanupBatchEntry& entry,Result rc)->Result{
        R_UNLESS(fprintf(log,"%s target=%u kind=%u app=%016llX id=%s result=%08X name=%s\n",status,unsigned(entry.item.target),unsigned(entry.item.kind),static_cast<unsigned long long>(entry.item.application),HexId(&entry.item.content).c_str(),unsigned(rc),entry.item.name.c_str())>=0&&fflush(log)==0&&fsync(fileno(log))==0,MAKERESULT(Module_Libnx,LibnxError_IoError));return 0;
    };
    const auto run=[&]()->Result{
        for(size_t index=0;index<batch.entries.size();++index){
            R_TRY(p->ShouldExitResult());const auto& entry=batch.entries[index];
            ui::ProgressScope item_progress(p,nexus::WorkPart(index,batch.entries.size()),nexus::WorkPart(index+1,batch.entries.size()));
            p->NewTransfer("Deleting selected cleanup items");
            p->SetTitle("Cleanup "+std::to_string(index+1)+" / "+std::to_string(batch.entries.size()));
            if(!entry.ready){++report.skipped;R_TRY(line("SKIPPED_PREVIEW",entry,entry.result));continue;}
            GameRemovalPlan fresh;const auto checked=PreviewGameRemoval(p,entry.item,fresh);R_TRY(p->ShouldExitResult());
            if(R_FAILED(checked)||RemovalFingerprint(fresh)!=entry.selection){++report.skipped;R_TRY(line("SKIPPED_CHANGED",entry,checked?checked:Invalid));continue;}
            R_TRY(line("APPROVED",entry,0));GameRemovalReport one;
            const auto rc=ApplyGameRemoval(p,fresh,one);report.restart|=one.restart;report.files+=one.removed;report.bytes+=one.bytes;
            if(R_FAILED(rc)){++report.failed;R_TRY(line("STOPPED",entry,rc));return rc;}
            ++report.removed;R_TRY(line("REMOVED",entry,0));
        }
        return 0;
    };
    const auto rc=run();report.complete=R_SUCCEEDED(rc);
    report.detail="Removed "+std::to_string(report.removed)+" cleanup item(s). Skipped "+std::to_string(report.skipped)+" that could not be verified or changed since review. Saved progress was kept.";
    if(R_FAILED(rc))report.detail+=" Cleanup stopped. Review what remains before retrying.";
    if(report.restart)report.detail+=" Fully restart the Switch before launching or reinstalling USB games.";
    report.detail+=" Report: switch/nexusmanager/cleanup-all-last.log";
    return rc;
}
}
