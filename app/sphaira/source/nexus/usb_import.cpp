#include "nexus/problem_reports.hpp"
#include "nexus/localize.hpp"
#include "nexus/routing_backup.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_import.hpp"
#include "nexus/usb_import_core.hpp"
#include "nexus/usb_import_diagnostic.hpp"
#include "nexus/usb_import_guard.hpp"
#include "nexus/usb_ticket_store.hpp"
#include "nexus/bootstrap.h"
#include "nexus/manager.hpp"
#include "nexus/home_badges.hpp"
#include "nexus/completion.hpp"
#include "nexus/safety.hpp"
#include "yati/nx/ns.hpp"
#include "defines.hpp"
#include <map>
#include <cstring>
#include <tuple>

namespace sphaira::nexusapp {
namespace {
namespace core=nexus::usbimport;
using nexus::repair::Image;
using nexus::repair::Snapshot;
using nexus::recovery::Bytes;
constexpr Result Missing=MAKERESULT(2,1);
constexpr Result WrongImportSystem=MAKERESULT(505,902);
Result SystemIdentity(std::array<u8,32>& out){
    out={};
    // Atmosphere's existing GetEmummcConfig ABI, also used by app.cpp:
    // X[1..] holds EFS0 base config and (for partition mode) start sector;
    // the page-aligned output holds file and Nintendo paths, 128 bytes each.
    struct Config {u32 magic,type,id,fs_version;u64 start_sector;};
    static_assert(sizeof(Config)==24);
    alignas(0x1000) char paths[256]{};
    SecmonArgs args{};args.X[0]=0xF0000404;args.X[1]=0;args.X[2]=reinterpret_cast<u64>(paths);
    svcCallSecureMonitor(&args);
    R_UNLESS(args.X[0]==0,nexus::Invalid);
    Config config{};memcpy(&config,&args.X[1],sizeof(config));
    // Hekate leaves the entire emuMMC config zeroed when booting sysMMC.
    // Atmosphere returns that NONE config without adding the EFS0 magic.
    // Accept only the completely empty form; a partial/active config with an
    // invalid magic is still unsafe to bind to a recovery journal.
    const bool empty_sysmmc=config.magic==0 && config.type==0 && config.id==0 &&
        config.fs_version==0 && config.start_sector==0 &&
        std::all_of(std::begin(paths),std::end(paths),[](char c){return c==0;});
    R_UNLESS((config.magic==0x30534645 || empty_sysmmc) && config.type<=2 && memchr(paths,0,128) && memchr(paths+128,0,128),nexus::Invalid);
    if(config.type==1)R_UNLESS(config.start_sector,nexus::Invalid);
    if(config.type==2)R_UNLESS(paths[0],nexus::Invalid);
    R_TRY(splInitialize());ON_SCOPE_EXIT(splExit());
    u64 device{};R_TRY(splGetConfig(SplConfigItem_DeviceId,&device));R_UNLESS(device,nexus::Invalid);
    // A cloned card can have the same emuMMC paths and partition layout.
    // Bind rollback to the physical card too; an unavailable CID never falls
    // back to the console/layout-only identity.
    std::array<u8,16> cid{};FsDeviceOperator sd_device{};R_TRY(fsOpenDeviceOperator(&sd_device));
    const auto cid_result=fsDeviceOperatorGetSdCardCid(&sd_device,cid.data(),cid.size(),cid.size());
    fsDeviceOperatorClose(&sd_device);R_TRY(cid_result);
    R_UNLESS(std::any_of(cid.begin(),cid.end(),[](u8 b){return b!=0;}),nexus::Invalid);
    // Canonical length-prefixed fields exclude padding and firmware version.
    // Only the digest enters the journal; raw device IDs/paths are not logged.
    Bytes bytes;constexpr char domain[]="Nexus USB import system v2";
    bytes.insert(bytes.end(),domain,domain+sizeof(domain));nexus::recovery::U64(bytes,device);
    bytes.insert(bytes.end(),cid.begin(),cid.end());
    nexus::recovery::U32(bytes,config.type);nexus::recovery::U32(bytes,config.id);
    if(config.type==1)nexus::recovery::U64(bytes,config.start_sector);
    const auto path=[&](const char* p){const auto length=strlen(p);nexus::recovery::U32(bytes,length);bytes.insert(bytes.end(),p,p+length);};
    if(config.type==2)path(paths);
    path(paths+128);sha256CalculateHash(out.data(),bytes.data(),bytes.size());
    R_UNLESS(core::ValidSystem(out),nexus::Invalid);return 0;
}
struct Path {char value[FS_MAX_PATH]{};explicit Path(const std::string& p){snprintf(value,sizeof(value),"%s",p.c_str());}};
core::Key Key(const NcmContentMetaKey& k){return {k.id,k.version,k.type,k.install_type};}
NcmContentMetaKey Key(const core::Key& k){NcmContentMetaKey out{};out.id=k.id;out.version=k.version;out.type=k.type;out.install_type=k.install;return out;}
std::string Label(const UsbImportTitle& row){char id[17];snprintf(id,sizeof(id),"%016lX",row.record.key.id);return row.name+" ["+id+"] v"+std::to_string(row.record.key.version);}
bool SameRecord(const ncm::ContentStorageRecord& a,const ncm::ContentStorageRecord& b){return nexus::SameMetaKey(a.key,b.key)&&a.storage_id==b.storage_id;}
Bytes Records(std::vector<ncm::ContentStorageRecord> rows){
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return std::tuple(a.key.id,a.key.type,a.key.version,a.key.install_type,a.storage_id)<std::tuple(b.key.id,b.key.type,b.key.version,b.key.install_type,b.storage_id);});
    Bytes out;
    for(const auto& r:rows){ncm::ContentStorageRecord clean{};clean.key=Key(Key(r.key));clean.storage_id=r.storage_id;
        const auto* p=reinterpret_cast<const u8*>(&clean);out.insert(out.end(),p,p+sizeof(clean));}
    return out;
}
std::vector<ncm::ContentStorageRecord> Records(const Bytes& bytes){
    std::vector<ncm::ContentStorageRecord> out(bytes.size()/sizeof(ncm::ContentStorageRecord));
    if(!bytes.empty())memcpy(out.data(),bytes.data(),bytes.size());
    return out;
}
static_assert(sizeof(ncm::ContentStorageRecord)==24);
struct Disk {
    FsFileSystem sd{};NcmContentMetaDatabase db{};bool ncm_open{},ns_open{};
    nexus::Client usb;ui::ProgressBox* progress{};u64 generation{};Result rc{};
    const char* step{"checking the import request"_nx};
    ~Disk(){ncmContentMetaDatabaseClose(&db);if(ns_open)ns::Exit();if(ncm_open)ncmExit();if(serviceIsActive(&sd.s))fsFsClose(&sd);}
    bool Ok(Result r){if(R_FAILED(r)){rc=r;return false;}return true;}
    bool SystemMatches(const core::Journal& j){
        step="identifying this system and physical SD card"_nx;
        std::array<u8,32> current{};if(!Ok(SystemIdentity(current)))return false;
        return current==j.system?true:Ok(WrongImportSystem);
    }
    Result Open(bool services){
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            step="checking the physical SD view"_nx;
            NexusVsdStatus vs{};R_TRY(usb.VirtualSdStatus(vs));R_UNLESS(!(vs.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);
        }
        step="opening the physical SD filesystem"_nx;R_TRY(fsOpenSdCardFileSystem(&sd));
        if(services){
            step="opening the content manager service"_nx;R_TRY(ncmInitialize());ncm_open=true;
            step="opening the HOME registration service"_nx;R_TRY(ns::Initialize());ns_open=true;
            step="opening the SD content database"_nx;R_TRY(ncmOpenContentMetaDatabase(&db,NcmStorageId_SdCard));
        }
        return 0;
    }
    bool Check(){
        if(progress&&!Ok(progress->ShouldExitResult())){step="checking cancellation"_nx;return false;}
        if(generation&&!Ok(usb.Check(generation))){step="checking the connected USB drive"_nx;return false;}
        return true;
    }
    bool Read(const std::string& name,Image& out){
        step="reading an import backup or routing index"_nx;
        out={};FsFile f{};auto r=fsFsOpenFile(&sd,Path(name).value,FsOpenMode_Read,&f);if(r==Missing)return true;if(!Ok(r))return false;
        s64 size{};r=fsFileGetSize(&f,&size);u64 got{};
        if(R_SUCCEEDED(r)&&(size<0||static_cast<u64>(size)>core::MaxJournal))r=nexus::Invalid;
        if(R_SUCCEEDED(r)){out.data.resize(size);r=fsFileRead(&f,0,out.data.data(),out.data.size(),FsReadOption_None,&got);}
        fsFileClose(&f);if(!Ok(r))return false;if(got!=out.data.size())return Ok(nexus::Invalid);out.exists=true;return true;
    }
    bool WriteNew(const std::string& name,const Bytes& data){
        step="writing and checking the import backup"_nx;
        if(!Ok(fsFsCreateFile(&sd,Path(name).value,data.size(),0)))return false;
        FsFile f{};if(!Ok(fsFsOpenFile(&sd,Path(name).value,FsOpenMode_Write,&f)))return false;
        const auto r=fsFileWrite(&f,0,data.data(),data.size(),FsWriteOption_Flush);fsFileClose(&f);
        Image check;return Ok(r)&&Read(name,check)&&check==Image{true,data};
    }
    bool Begin(const std::string& name){step="creating the import backup directory"_nx;return Ok(fsFsCreateDirectory(&sd,Path(name).value));}
    bool Directory(const std::string& name){step="checking the import backup directory"_nx;FsDirEntryType type{};const auto r=fsFsGetEntryType(&sd,Path(name).value,&type);return r==Missing?Begin(name):(Ok(r)&&type==FsDirEntryType_Dir);}
    bool Commit(){step="committing the import backup to SD"_nx;return Ok(fsFsCommit(&sd));}
    bool Rename(const std::string& from,const std::string& to){step="publishing the import recovery marker"_nx;return Ok(fsFsRenameFile(&sd,Path(from).value,Path(to).value));}
    void Activated(){} // Outer import journal already owns recovery.
    bool Indexes(Snapshot& out){for(size_t i=0;i<3;++i)if(!Read(nexus::repair::Live(i),out[i]))return false;return true;}
    bool MetaRead(const core::Key& raw,Image& out){
        step="reading existing SD content metadata"_nx;
        out={};const auto key=Key(raw);bool exists{};if(!Ok(ncmContentMetaDatabaseHas(&db,&exists,&key)))return false;if(!exists)return true;
        u64 size{},got{};if(!Ok(ncmContentMetaDatabaseGetSize(&db,&size,&key))||size<8||size>core::MaxMeta)return false;
        out.data.resize(size);if(!Ok(ncmContentMetaDatabaseGet(&db,&key,&got,out.data.data(),size))||got!=size)return false;out.exists=true;return true;
    }
    bool MetaWrite(const core::Key& raw,const Image& wanted){
        step="writing and checking SD content metadata"_nx;
        Image current;if(!MetaRead(raw,current))return false;if(current==wanted)return true;
        const auto key=Key(raw);
        if(!Ok(wanted.exists?ncmContentMetaDatabaseSet(&db,&key,wanted.data.data(),wanted.data.size()):ncmContentMetaDatabaseRemove(&db,&key)))return false;
        return Ok(ncmContentMetaDatabaseCommit(&db))&&MetaRead(raw,current)&&current==wanted;
    }
    bool HomeRead(u64 app,Image& out,u8* event=nullptr){
        step="reading existing HOME game records"_nx;
        out={};bool complete=false;
        for(s32 offset=0;offset<65536;offset+=128){NsApplicationRecord page[128]{};s32 count{};
            if(!Ok(nsListApplicationRecord(page,128,offset,&count))||count<0||count>128)return false;
            for(s32 i=0;i<count;++i)if(page[i].application_id==app){out.exists=true;if(event)*event=page[i].last_event;}
            if(count<128){complete=true;break;}}
        if(!complete)return false;
        std::vector<ncm::ContentStorageRecord> rows;if(!Ok(ns::GetApplicationRecords(app,rows))||rows.size()>core::MaxRecords)return false;
        if(!out.exists && !rows.empty())return false;
        out.data=Records(std::move(rows));return true;
    }
    bool HomeWrite(u64 app,const Image& wanted,u8 event){
        step="writing and checking HOME game records"_nx;
        Image current;u8 actual_event{};if(!HomeRead(app,current,&actual_event))return false;
        if(current.exists&&(actual_event==ns::ApplicationRecordType_Running||actual_event==ns::ApplicationRecordType_Downloading))return false;
        if(current==wanted&&(!wanted.exists||actual_event==event))return true;
        const auto rows=Records(wanted.data);
        if(!Ok(wanted.exists?ns::PushApplicationRecordWithType(app,rows.data(),rows.size(),event):ns::DeleteApplicationRecord(app)))return false;
        return HomeRead(app,current,&actual_event)&&current==wanted&&(!wanted.exists||actual_event==event);
    }
    bool HomeIntermediate(const Image& now,const core::Journal& j){
        if(!now.exists)return now==j.home_before;
        const auto before=Records(j.home_before.data),after=Records(j.home_after.data),current=Records(now.data);
        auto contains=[](const auto& list,const auto& r){return std::any_of(list.begin(),list.end(),[&](const auto& v){return SameRecord(v,r);});};
        for(const auto& r:current)if(!contains(before,r)&&!contains(after,r))return false;
        // Preserve every unrelated application record. Each changed title is
        // either at its original value or one complete planned publication.
        std::set<std::pair<u64,u8>> groups;
        for(const auto& r:before)groups.insert({r.key.id,r.key.type});
        for(const auto& r:after)groups.insert({r.key.id,r.key.type});
        for(const auto& g:groups){
            auto subset=[&](const auto& all){std::vector<ncm::ContentStorageRecord> rows;for(const auto& r:all)if(r.key.id==g.first&&r.key.type==g.second)rows.push_back(r);return Records(rows);};
            const auto c=subset(current);if(c!=subset(before)&&c!=subset(after))return false;
        }
        return true;
    }
    bool IndexesRestore(const core::Journal& j){
        step="restoring the previous routing indexes"_nx;
        Image pending;if(!Read(nexus::repair::Pending,pending))return false;
        if(pending.exists){
            nexus::repair::Journal r;if(!nexus::repair::DecodeJournal(pending.data,r))return false;
            const bool forward=r.before==j.before&&r.after==j.after;
            const bool backward=r.after==j.before&&core::RestorableIndexes(r.before,j);
            if(!forward&&!backward)return false;
            if(!nexus::repair::Restore(*this,r))return false;
        }
        Snapshot current;if(!Indexes(current))return false;if(current==j.before)return true;
        if(!core::RestorableIndexes(current,j))return false;
        return nexus::repair::Publish(*this,{armGetSystemTick(),current,j.before});
    }
    bool Finish(const core::Journal& j,bool success){
        step="committing the import recovery marker"_nx;
        const auto destination=core::Stage(j)+(success?"/completed.bin":"/rolled-back.bin");
        if(!Rename(core::Pending,destination))return false;
        if(Commit())return true;
        const auto failure=rc;
        // The rename is visible before its commit. A failed commit must not
        // silently clear the install gate: put the verified journal back and
        // retain failure even if the recovery write succeeds. If renaming it
        // back fails, a new identical marker is safer than an unguarded state.
        Image pending;
        if(!Read(core::Pending,pending)||!pending.exists){
            Image moved;const bool ours=Read(destination,moved)&&moved==Image{true,core::Encode(j)};
            if(ours&&Rename(destination,core::Pending))Commit();
            else if(WriteNew(core::Pending,core::Encode(j)))Commit();
        }
        rc=failure;return false;
    }
};
Result Failure(const Disk& d){return R_FAILED(d.rc)?d.rc:nexus::Invalid;}
bool SnapshotFamily(Disk& d,const std::vector<const UsbImportTitle*>& rows,core::Journal& j){
    j={};j.nonce=armGetSystemTick();j.application=rows.front()->record.application_id;
    d.step="identifying this system and physical SD card"_nx;
    if(!d.Ok(SystemIdentity(j.system)))return false;
    if(!d.Indexes(j.before)||!d.HomeRead(j.application,j.home_before,&j.home_event))return false;
    // Refuse changes under a running application or an unrelated incomplete install.
    d.step="checking whether the game is running or downloading"_nx;
    if(j.home_before.exists&&(j.home_event==ns::ApplicationRecordType_Running||j.home_event==ns::ApplicationRecordType_Downloading))return false;
    d.step="checking the existing USB routing index format"_nx;
    nexus::repair::Indexes before,after;if(!nexus::repair::Decode(j.before,before))return false;after=before;
    auto home=Records(j.home_before.data);
    for(const auto* row:rows){const auto& record=row->record;
        core::Meta m;m.key=Key(record.key);if(!d.MetaRead(m.key,m.before))return false;m.after={true,record.installed_meta};
        // Never overwrite different pre-existing metadata from another source.
        d.step="checking for conflicting content metadata"_nx;
        if(m.before.exists&&m.before!=m.after)return false;
        d.step="checking the existing HOME version and storage location"_nx;
        for(const auto& r:home)if(r.key.id==record.key.id&&r.key.type==record.key.type &&
            (r.storage_id!=NcmStorageId_SdCard || !nexus::SameMetaKey(r.key,record.key)))return false;
        j.metadata.push_back(std::move(m));
        std::vector<nexus::recovery::Content> content;
        for(const auto& c:record.content){nexus::recovery::Content out{};memcpy(out.id.data(),&c.info.content_id,16);ncmContentInfoSizeToU64(&c.info,&out.size);out.type=c.info.content_type;out.offset=c.info.id_offset;content.push_back(out);}
        d.step="checking for conflicting USB routing entries"_nx;
        const auto merged=nexus::repair::Merge(after,record.key.id,record.key.type==NcmContentMetaType_Application,content);
        if(merged!=nexus::repair::MergeResult::Changed&&merged!=nexus::repair::MergeResult::Intact)return false;
        std::erase_if(home,[&](const auto& r){return r.key.id==record.key.id&&r.key.type==record.key.type;});
        ncm::ContentStorageRecord r{};r.key=record.key;r.storage_id=NcmStorageId_SdCard;home.push_back(r);
    }
    j.after=nexus::repair::Output(j.before,before,after);j.home_after={true,Records(home)};
    for(size_t i=0;i<2;++i){char name[128];snprintf(name,sizeof(name),"/atmosphere/usb_badges/%016lx-%zu.ubg",j.application,i);if(!d.Read(name,j.badges[i]))return false;}
    d.step="validating the import recovery snapshot"_nx;
    const auto bytes=core::Encode(j);core::Journal check;return core::Decode(bytes,check);
}
bool Unchanged(Disk& d,const core::Journal& j){
    Snapshot current;Image home;if(!d.Indexes(current)||current!=j.before||!d.HomeRead(j.application,home)||home!=j.home_before)return false;
    for(const auto& m:j.metadata){Image actual;if(!d.MetaRead(m.key,actual)||actual!=m.before)return false;}return true;
}
bool Activate(Disk& d,const core::Journal& j){
    const auto stage=core::Stage(j);Image pending,repair;
    return d.SystemMatches(j)&&d.Check()&&d.Read(core::Pending,pending)&&!pending.exists&&d.Read(nexus::repair::Pending,repair)&&!repair.exists&&
        d.Begin(stage)&&d.WriteNew(stage+"/journal.bin",core::Encode(j))&&d.Commit()&&d.Check()&&Unchanged(d,j)&&
        d.Rename(stage+"/journal.bin",core::Pending)&&d.Commit();
}
void SaveReport(Disk& d,UsbImportResult& out){
    if(!d.Directory("/switch")||!d.Directory("/switch/nexusmanager")||!d.Directory("/switch/nexusmanager/recovery-reports"))return;
    char name[160];snprintf(name,sizeof(name),"/switch/nexusmanager/recovery-reports/import-%016lX.txt",armGetSystemTick());
    char code[24];snprintf(code,sizeof(code),"0x%08X",out.result);
    std::string text="USB library import\nResult: "_nx+std::string(code)+"\n"+out.detail+"\nImported records: "_nx+std::to_string(out.imported)+"\nSkipped records: "_nx+std::to_string(out.skipped)+"\n\n";
    for(const auto& entry:out.entries)text+=entry+"\n";
    text+="\nNo NCA files or saves were copied or deleted. Valid imported tickets are retained if registration is rolled back. Index and system-record backups remain in atmosphere/usb-import-* on this SD.\n"_nx;
    const Bytes bytes(text.begin(),text.end());const std::string tmp=std::string(name)+".tmp";
    if(d.WriteNew(tmp,bytes)&&d.Commit()&&d.Rename(tmp,name)&&d.Commit())out.report_path="SD:"+std::string(name);
}
}
Result UsbImportPending(bool& pending){pending=false;Disk d;R_TRY(d.Open(false));Image f;R_UNLESS(d.Read(core::Pending,f),Failure(d));pending=f.exists;return 0;}
Result RestoreUsbImport(ui::ProgressBox* p,UsbImportResult& out){
    out={};Disk d;
    const auto operation=[&]()->Result{
    d.step="checking the restore request"_nx;R_UNLESS(p,nexus::Invalid);R_TRY(d.Open(true));Image pending;R_UNLESS(d.Read(core::Pending,pending),Failure(d));R_UNLESS(pending.exists,nexus::Invalid);
    out.pending=true;
    core::Journal j;
    if(!core::Decode(pending.data,j)){
        out.detail="This import backup cannot be verified or has no system identity. Nothing was restored. Keep the pending import and backup folders for diagnosis."_nx;
        return nexus::Invalid;
    }
    if(!d.SystemMatches(j)){
        out.detail=d.rc==WrongImportSystem?
            "This import backup belongs to a different emuMMC or system on this console. Boot the original emuMMC/system, then restore it there. No records or files were changed; the pending import remains protected."_nx:
            "The running system could not be identified safely. Boot the original emuMMC/system before restoring this import. No records or files were changed; keep the pending import backup."_nx;
        return Failure(d); // No report write or rollback in the wrong system.
    }
    p->NewTransfer("Restoring USB import backup"_nx);out.restart=out.pending=true;
    // Once restoration starts it must complete despite a cancellation request;
    // it uses only physical SD/system snapshots, not the now disconnected USB.
    nexus::usbimport::WorkerScope worker;
    const bool ok=core::Restore(d,j);out.rolled_back=ok;out.pending=!ok;
    out.detail=ok?"Previous game records and routing restored. Fully restart the Switch before continuing."_nx:"Restoration stopped. Keep the import and repair backups; do not install or move games. State changed or storage could not be written."_nx;
    const auto result=ok?0:Failure(d);out.result=result;
    if(R_FAILED(result))out.detail=core::FailureDetail(d.step,result,out.detail);
    SaveReport(d,out);return result;
    };
    const auto result=operation();out.result=result;
    if(R_SUCCEEDED(result)&&out.rolled_back&&!out.pending)BackupConsoleRoutingAfterChange();
    if(R_FAILED(result)&&out.detail.empty())out.detail=core::FailureDetail(d.step,result);
    return result;
}
Result ImportUsbLibrary(ui::ProgressBox* p,const UsbImportScan& scan,UsbImportResult& out){
    const auto operation=[&]()->Result{
    out={};Disk d;d.progress=p;d.generation=scan.generation;
    const auto operation=[&]()->Result{
    R_UNLESS(p&&scan.complete&&scan.generation,nexus::Invalid);
    R_TRY(d.Open(true));
    d.step="checking unfinished USB recovery work"_nx;R_TRY(nexus::CheckUsbMutationAllowed());
    d.step="checking USB firmware compatibility"_nx;R_TRY(nexus::CheckUsbFirmware());
    R_UNLESS(d.Check(),Failure(d));
    nexus::usbimport::WorkerScope worker;
    std::map<u64,std::vector<const UsbImportTitle*>> groups;
    const auto plan=PlanUsbLibrary(scan);
    std::set<const UsbImportTitle*> handled;
    for(size_t i=0;i<scan.titles.size();++i){const auto& row=scan.titles[i];
        if(plan.selected[i])groups[row.record.application_id].push_back(&row);
        else {
            handled.insert(&row);++out.skipped;
            const auto reason=!plan.family_reason[i].empty()?plan.family_reason[i]+" No registration changes will be made for this family."_nx:
                row.status==UsbImportStatus::Ready?"A newer version is on this drive; the older version was not selected."_nx:std::string(UsbImportStatusText(row.status))+". "+row.reason;
            out.entries.push_back("Skipped: "+Label(row)+" - "+reason);
        }
    }
    Result final{};
    for(auto& [application,rows]:groups){
        std::sort(rows.begin(),rows.end(),[](const auto* a,const auto* b){return a->record.key.type<b->record.key.type;});
        core::Journal j;
        if(!d.Check()){final=Failure(d);break;}
        if(!SnapshotFamily(d,rows,j)){
            const bool failed=R_FAILED(d.rc);
            const auto reason=failed?core::FailureDetail(d.step,d.rc):
                std::string("The existing state does not allow import while "_nx)+d.step+". Check this title's existing installation before trying again."_nx;
            for(const auto* row:rows){handled.insert(row);++out.skipped;out.entries.push_back("Not added: "_nx+Label(*row)+" - "+reason+" No registration changes were made for this game."_nx);}
            if(failed){out.detail=reason;final=d.rc;break;}
            continue;
        }
        // Hash all records in the family before any live registration. A bad
        // update must not leave a partially imported base game behind.
        d.step="verifying existing USB content hashes"_nx;
        for(const auto* row:rows){final=yati::VerifyExistingUsbRecord(p,scan.generation,row->record);if(R_FAILED(final))break;}
        if(R_FAILED(final)){for(const auto* row:rows){handled.insert(row);++out.skipped;out.entries.push_back("Not imported: "_nx+Label(*row)+" - verification failed or the scan was cancelled; no registration for this game."_nx);}break;}
        if(!d.Directory("/atmosphere")||!d.Directory(nexus::repair::Folder)||!Activate(d,j)){
            // Rename may have succeeded before an SD commit error. Check the
            // marker rather than assuming failure means nothing was activated.
            final=Failure(d);out.detail=core::FailureDetail(d.step,final);
            Image marker;out.pending=!d.Read(core::Pending,marker)||marker.exists;out.restart|=out.pending;break;
        }
        out.restart=out.pending=true;
        final=[&]()->Result{
            d.step="restoring saved tickets for this system"_nx;
            std::set<std::array<u8,16>> imported_tickets;
            for(const auto* row:rows)for(const auto& rights:row->archived_tickets){std::array<u8,16> id{};memcpy(id.data(),&rights,16);if(imported_tickets.insert(id).second)R_TRY(ImportUsbTicketPair(scan.generation,rights));}
            d.step="publishing USB routing indexes"_nx;
            if(j.before!=j.after)R_UNLESS(nexus::repair::Publish(d,{j.nonce,j.before,j.after}),Failure(d));
            d.step="registering existing USB content and HOME records"_nx;
            for(const auto* row:rows){R_UNLESS(d.Check(),Failure(d));R_TRY(yati::PublishExistingUsbRecord(p,scan.generation,row->record,0));}
            Snapshot indexes;Image home;R_UNLESS(d.Indexes(indexes)&&core::SameIndexes(indexes,j.after)&&d.HomeRead(application,home)&&home==j.home_after,Failure(d));
            for(const auto& m:j.metadata){Image actual;R_UNLESS(d.MetaRead(m.key,actual)&&actual==m.after,Failure(d));}
            R_UNLESS(d.Check()&&d.Finish(j,true),Failure(d));return 0;
        }();
        if(R_FAILED(final)){
            out.detail=core::FailureDetail(d.step,final);
            // No USB access or cancellation is required for safe local rollback.
            d.progress=nullptr;d.generation=0;Image marker;
            const bool checked=d.Read(core::Pending,marker);
            if(checked&&marker.exists){out.rolled_back=core::Restore(d,j);out.pending=!out.rolled_back;}else out.pending=true;
            for(const auto* row:rows){handled.insert(row);++out.skipped;out.entries.push_back("Import stopped: "_nx+Label(*row)+(out.rolled_back?" - previous registration restored."_nx:" - recovery needs checking before further changes."_nx));}
            break;
        }
        out.pending=false;out.imported+=rows.size();
        for(const auto* row:rows){handled.insert(row);out.entries.push_back("Imported: "+Label(*row));}
        const auto launch=ns::SetLowestLaunchVersion(application);
        if(R_FAILED(launch))out.entries.push_back("Game registration completed, but its launch-version refresh could not be confirmed. Keep the report if HOME asks for an update."_nx);
        // Cosmetic refresh happens only after registration commits. Its retry
        // receipt survives reboot; failure never undoes a verified install.
        auto badge=QueueHomeRefresh(application);
        if(R_SUCCEEDED(badge)){Game game;game.id=application;game.badge_state=1;badge=UpdateHomeBadge(game);if(R_SUCCEEDED(badge))badge=FinishHomeRefresh(application);}
        if(R_FAILED(badge))out.entries.push_back("HOME icon refresh pending for "_nx+std::to_string(application)+". Use Refresh HOME USB icons after restarting."_nx);
    }
    for(const auto& row:scan.titles)if(!handled.contains(&row)){++out.skipped;out.entries.push_back("Not attempted: "_nx+Label(row)+" - import stopped before this title completed. Scan again after recovery/restart."_nx);}
    const auto failure_context=out.detail;
    if(out.pending)out.detail="Repair stopped. Open Installed games > Repair USB games > Restore backup before installing or moving anything. Keep all backup folders."_nx;
    else if(R_FAILED(final))out.detail=out.rolled_back?"Import stopped; the current game's original registration was restored. Earlier completed imports are kept. Fully restart the Switch."_nx:"Import stopped. Review the report before trying again."_nx;
    else if(plan.summary.blocked_games)out.detail=std::string(out.imported?"Repair completed for the repairable game families. "_nx:"No game families were repaired. "_nx)+
        std::to_string(plan.summary.blocked_games)+" game families were blocked and left unchanged. Review their missing-content, permission or registration checks before retrying. "_nx+
        (out.restart?"Fully restart the Switch before playing."_nx:"No registration changes were made."_nx);
    else out.detail="Import finished. Existing USB files were reused. "_nx+std::string(out.restart?"Fully restart the Switch before playing."_nx:"No registration changes were needed."_nx);
    if(!failure_context.empty())out.detail+="\n\n"+failure_context;
    else if(R_FAILED(final))out.detail=core::FailureDetail(d.step,final,out.detail);
    out.result=final;SaveReport(d,out);return final;
    };
    const auto result=operation();out.result=result;
    // Earlier families remain committed if a later family fails and rolls back.
    if(out.imported&&!out.pending)BackupConsoleRoutingAfterChange();
    if(R_FAILED(result)&&out.detail.empty())out.detail=core::FailureDetail(d.step,result);
    return result;

    };
    const auto result=operation();
    if(!p || !p->ShouldExit())nexus::reports::QueueFailure("import",result);
    return result;
}
}
