// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/index_recovery.hpp"
#include "nexus/usb_import_scan.hpp"
#include "yati/nx/content_hash.hpp"
#include "yati/nx/keys.hpp"
#include "nexus/bootstrap.h"
#include "nexus/update.hpp"
#include "defines.hpp"
#include "title_info.hpp"
#include <map>
#include <tuple>
#include <cstdio>
#include <cstring>
namespace sphaira::nexusapp {
namespace {
constexpr const char* Folder="/atmosphere/usb_content";
constexpr Result MissingUsb=MAKERESULT(140,205), MissingSd=MAKERESULT(2,1);
namespace core=nexus::recovery;
namespace repair=nexus::repair;
// libnx sends FS_MAX_PATH bytes for filesystem path descriptors.
struct NativePath {
    char value[FS_MAX_PATH]{};
    explicit NativePath(const char* p){snprintf(value,sizeof(value),"%s",p);}
};
class Session {
public:
    nexus::Client usb;
    FsFileSystem sd{};
    NcmContentStorage native{},routed{};
    NcmContentMetaDatabase db{};
    ui::ProgressBox* progress;
    RecoveryPlan& plan;
    bool sd_open{},ncm_open{};
    Session(ui::ProgressBox* p,RecoveryPlan& r):progress(p),plan(r){}
    ~Session(){ncmContentMetaDatabaseClose(&db);ncmContentStorageClose(&routed);ncmContentStorageClose(&native);if(sd_open)fsFsClose(&sd);if(ncm_open)ncmExit();}
    Result Error(Result r,const char* text){plan.detail=text;return R_FAILED(r)?r:nexus::Invalid;}
    Result Check(){
        auto r=progress->ShouldExitResult();
        if(R_SUCCEEDED(r))r=usb.Check(plan.generation);
        if(R_FAILED(r))return Error(r,plan.published?
            "Repair interrupted. Fully restart, then run Help & options > Repair USB library to restore the saved indexes before installing anything.":
            "Cancelled or USB disconnected/changed. No live indexes were changed.");
        return 0;
    }
    Result Open(){
        // Do not mistake a USB virtual-sdmc session for physical SD.
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            NexusVsdStatus vs{};R_TRY(usb.VirtualSdStatus(vs));
            R_UNLESS(!(vs.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Invalid);
        }
        R_TRY(fsOpenSdCardFileSystem(&sd));sd_open=true;
        nexus::Status status{};R_TRY(usb.Query(status));R_UNLESS(status.Available(),nexus::Unavailable);
        if(plan.generation)R_UNLESS(plan.generation==status.generation,nexus::Unavailable);
        plan.generation=status.generation;
        R_TRY(ncmInitialize());ncm_open=true;
        R_TRY(nexus::CheckUsbFirmware());
        R_TRY(OpenStorage(Target::Sd,native)); // explicit physical-SD NCM view
        R_TRY(OpenStorage(Target::Usb,routed));
        R_TRY(ncmOpenContentMetaDatabase(&db,NcmStorageId_SdCard));
        return Check();
    }
    Result FolderExists(bool& exists){
        const NativePath path(Folder);
        FsDirEntryType type{};const auto r=fsFsGetEntryType(&sd,path.value,&type);
        exists=R_SUCCEEDED(r);
        if(exists && type!=FsDirEntryType_Dir)return Error(nexus::Invalid,"usb_content is a file, not a directory. Nothing was changed.");
        return r==MissingSd?0:r;
    }
    Result Read(const std::string& path,repair::Image& out){
        out={};const NativePath name(path.c_str());FsFile f{};
        auto r=fsFsOpenFile(&sd,name.value,FsOpenMode_Read,&f);
        if(r==MissingSd)return 0;
        R_TRY(r);s64 size{};r=fsFileGetSize(&f,&size);
        if(R_SUCCEEDED(r) && (size<0 || size>static_cast<s64>(repair::MaxJournal)))r=nexus::Invalid;
        u64 got{};
        if(R_SUCCEEDED(r)){out.data.resize(size);r=fsFileRead(&f,0,out.data.data(),out.data.size(),FsReadOption_None,&got);}
        fsFileClose(&f);R_TRY(r);R_UNLESS(got==out.data.size(),nexus::Invalid);out.exists=true;return 0;
    }
    Result Snapshot(repair::Snapshot& out){
        for(size_t i=0;i<3;++i)R_TRY(Read(repair::Live(i),out[i]));
        return 0;
    }
    Result Raw(const NcmContentMetaKey& key,std::vector<u8>& raw){
        u64 size{},got{};R_TRY(ncmContentMetaDatabaseGetSize(&db,&size,&key));
        R_UNLESS(size>=sizeof(NcmContentMetaHeader) && size<=1024*1024,nexus::Invalid);
        raw.resize(size);R_TRY(ncmContentMetaDatabaseGet(&db,&key,&got,raw.data(),size));
        R_UNLESS(got==size,nexus::Invalid);return 0;
    }
    Result ContentPath(const NcmContentInfo& info,std::string& path){
        char common[FS_MAX_PATH]{};R_TRY(ncmContentStorageGetPath(&routed,common,sizeof(common),&info.content_id));
        R_UNLESS(nexus::ContentPath(common,false,path) && nexus::RegisteredFilePath(path),nexus::Invalid);return 0;
    }
    Result SameSize(const RecoveryContent& f,bool& valid){
        valid=false;R_TRY(Check());nexus::Info stat{};
        const auto r=usb.Stat(plan.generation,f.path,stat);
        if(r==MissingUsb)return 0;
        R_TRY(r);
        u64 size{};ncmContentInfoSizeToU64(&f.info,&size);
        valid=!stat.directory && stat.size==size;return 0;
    }
};
Result Parse(const std::vector<u8>& raw,std::vector<NcmContentInfo>& infos){
    NcmContentMetaHeader h{};if(raw.size()<sizeof(h))return nexus::Invalid;
    memcpy(&h,raw.data(),sizeof(h));
    const size_t off=sizeof(h)+h.extended_header_size;
    if(!h.content_count || h.content_count>4096 || off>raw.size() || h.content_count>(raw.size()-off)/sizeof(NcmContentInfo))return nexus::Invalid;
    const size_t end=off+h.content_count*sizeof(NcmContentInfo);
    if(h.content_meta_count>(raw.size()-end)/sizeof(NcmContentMetaInfo))return nexus::Invalid;
    infos.resize(h.content_count);memcpy(infos.data(),raw.data()+off,infos.size()*sizeof(NcmContentInfo));return 0;
}
core::Content Convert(const NcmContentInfo& info){
    core::Content c{};memcpy(c.id.data(),&info.content_id,16);ncmContentInfoSizeToU64(&info,&c.size);
    c.type=info.content_type;c.offset=info.id_offset;return c;
}
// Application IDs for updates/DLC come from bounded system metadata, never
// title-ID bit arithmetic or the filename of a potentially bad NCA.
u64 ApplicationId(const RecoveryMeta& m){
    if(m.key.type==NcmContentMetaType_Application)return m.key.id;
    NcmContentMetaHeader h{};memcpy(&h,m.raw.data(),sizeof(h));
    if(h.extended_header_size<sizeof(u64) || h.extended_header_size>m.raw.size()-sizeof(h))return 0;
    if(m.key.type==NcmContentMetaType_DataPatch){
        if(h.extended_header_size<sizeof(NcmDataPatchMetaExtendedHeader))return 0;
        NcmDataPatchMetaExtendedHeader ext{};memcpy(&ext,m.raw.data()+sizeof(h),sizeof(ext));return ext.application_id;
    }
    if(m.key.type!=NcmContentMetaType_Patch && m.key.type!=NcmContentMetaType_AddOnContent)return 0;
    u64 id{};memcpy(&id,m.raw.data()+sizeof(h),sizeof(id));return id;
}
const char* RecordKind(u8 type){
    switch(type){
        case NcmContentMetaType_Application:return "Base game/app";
        case NcmContentMetaType_Patch:return "Update";
        case NcmContentMetaType_AddOnContent:return "DLC";
        case NcmContentMetaType_DataPatch:return "DLC data patch";
        default:return "Content record";
    }
}
std::string TitleId(u64 id){char b[17];snprintf(b,sizeof(b),"%016llX",static_cast<unsigned long long>(id));return b;}
std::string SafeLabel(std::string name){
    for(auto& c:name)if(static_cast<unsigned char>(c)<32 || c==127)c=' ';
    return name.empty()?"Name unavailable (use title ID)":name;
}
u64 Family(const RecoveryMeta& m){return m.application_id?m.application_id:m.key.id;}
void CollectIssues(RecoveryPlan& plan){
    plan.issues.clear();std::set<u64> blocked;
    for(const auto& m:plan.metadata)for(auto i:m.files)if(plan.files[i].mismatch)blocked.insert(Family(m));
    plan.blocked_records=0;
    for(auto& m:plan.metadata){
        m.blocked=blocked.contains(Family(m));
        if(m.blocked)++plan.blocked_records;
        for(auto i:m.files){
            const auto& f=plan.files[i];if(!f.mismatch)continue;
            plan.issues.push_back({m.name,HexId(&f.info.content_id)+".nca",f.path,
                HexId(&f.info.content_id),HexId(f.actual_hash.data(),f.actual_hash.size()),RecordKind(m.key.type),
                m.application_id,m.key.id,m.key.version});
        }
    }
}
struct RepairIO {
    Session& s;Result rc{};
    bool Ok(Result r){if(R_FAILED(r)){rc=r;return false;}return true;}
    bool Check(){return Ok(s.Check());}
    bool Read(const std::string& path,repair::Image& f){return Ok(s.Read(path,f));}
    bool Begin(const std::string& path){const NativePath p(path.c_str());return Ok(fsFsCreateDirectory(&s.sd,p.value));}
    bool Commit(){return Ok(fsFsCommit(&s.sd));}
    bool Rename(const std::string& from,const std::string& to){const NativePath a(from.c_str()),b(to.c_str());return Ok(fsFsRenameFile(&s.sd,a.value,b.value));}
    void Activated(){
        s.plan.published=true;
        s.plan.detail="An index repair was started but is not verified complete. Fully restart, then run Help & options > Repair USB library before any installs. An interrupted repair can restore its saved original indexes. Keep the repair backup folder.";
    }
    bool WriteNew(const std::string& path,const core::Bytes& data){
        const NativePath name(path.c_str());if(!Ok(fsFsCreateFile(&s.sd,name.value,data.size(),0)))return false;
        FsFile f{};if(!Ok(fsFsOpenFile(&s.sd,name.value,FsOpenMode_Write,&f)))return false;
        const auto r=fsFileWrite(&f,0,data.data(),data.size(),FsWriteOption_Flush);fsFileClose(&f);
        repair::Image check;return Ok(r) && Read(path,check) && check==repair::Image{true,data};
    }
};
}
Result ScanUsbRecovery(ui::ProgressBox* p,RecoveryPlan& plan,u64 only_application){
    plan={};plan.only_application=only_application;plan.detail="Recovery scan failed. No files were changed.";
    p->SetProgressNote("Large games take longer to check.");
    p->SetOverallTransfer(0,10000,"Finding USB game files");
    Session s(p,plan);R_TRY(s.Open());R_TRY(s.FolderExists(plan.folder_exists));
    repair::Image pending;R_TRY(s.Read(repair::Pending,pending));
    if(pending.exists){
        if(only_application)return s.Error(nexus::registration::RepairPending,"A library index repair is already pending. Use Help & options > Repair USB library to restore that transaction first. No per-game changes made.");
        if(!plan.folder_exists || !repair::DecodeJournal(pending.data,plan.journal))return s.Error(nexus::Invalid,"An unreadable index-repair journal was found. Nothing was changed. Preserve usb_content and usb_content-repair files for diagnosis.");
        plan.pending=plan.ready=true;
        plan.detail="An earlier index repair was interrupted. Restore its saved original indexes first?\n\nThis restores only the three routing indexes. Other files and settings are preserved. If an index was changed by another app, restoration will stop without guessing.\n\nAfter restoration, fully restart and scan again to repair missing records.";
        return 0;
    }
    R_TRY(s.Snapshot(plan.before));repair::Indexes original,merged;
    if(!repair::Decode(plan.before,original))return s.Error(nexus::Invalid,"An existing routing index is malformed, truncated or unsupported. It was NOT treated as empty. Preserve usb_content and send the indexes for diagnosis; no files changed.");
    merged=original;
    // A bounded complete DB inventory, not a routed Has() result or directory
    // name heuristic. Incomplete inventories never enable the repair button.
    std::vector<NcmContentMetaKey> keys(16384);s32 total{},written{};
    R_TRY(ncmContentMetaDatabaseList(&s.db,&total,&written,keys.data(),keys.size(),NcmContentMetaType_Unknown,0,0,UINT64_MAX,NcmContentInstallType_Full));
    R_UNLESS(total>=0 && written==total && written<=static_cast<s32>(keys.size()),nexus::Invalid);
    keys.resize(written);
    std::map<std::pair<u64,u8>,NcmContentMetaKey> latest;
    for(const auto& k:keys){
        if(k.type<NcmContentMetaType_Application || k.type>NcmContentMetaType_DataPatch)continue;
        auto [it,added]=latest.emplace(std::pair{k.id,k.type},k);
        if(!added && k.version>it->second.version)it->second=k;
    }
    std::map<core::Id,size_t> unique;
    std::map<u64,std::string> names;
    for(const auto& [identity,key]:latest){
        R_TRY(s.Check());++plan.records;p->SetOverallTransfer(500*plan.records/latest.size(),10000,"Finding USB game files");
        RecoveryMeta meta{};meta.key=key;R_TRY(s.Raw(key,meta.raw));
        meta.application_id=ApplicationId(meta);
        if(only_application && meta.application_id!=only_application)continue;
        std::vector<NcmContentInfo> infos;
        if(R_FAILED(Parse(meta.raw,infos))){++plan.skipped;continue;}
        std::vector<RecoveryContent> files;std::vector<core::Content> candidates;bool complete=true,any=false,sd_program=false;
        for(const auto& info:infos){
            const auto c=Convert(info);if(!core::Valid(c)){complete=false;break;}
            RecoveryContent f{info,{}};R_TRY(s.ContentPath(info,f.path));
            bool found{};R_TRY(s.SameSize(f,found));any|=found;complete&=found;
            if(info.content_type==NcmContentType_Program){
                bool native{};R_TRY(ncmContentStorageHas(&s.native,&native,&info.content_id));sd_program|=native;
            }
            candidates.push_back(c);files.push_back(std::move(f));
        }
        if(!any){if(only_application)++plan.skipped;continue;} // never register physical SD-only content as USB
        if(!complete || sd_program){
            ++plan.skipped;continue;
        }
        const bool application=key.type==NcmContentMetaType_Application;
        const auto result=repair::Merge(merged,key.id,application,candidates);
        if(result==repair::MergeResult::Conflict){++plan.conflicts;++plan.skipped;continue;}
        if(result==repair::MergeResult::Limit){++plan.limits;++plan.skipped;continue;}
        if(result==repair::MergeResult::Invalid){++plan.skipped;continue;}
        ++plan.complete;
        if(result==repair::MergeResult::Intact){++plan.intact;if(!only_application)continue;}
        if(!plan.folder_exists && !core::Add(plan.maps,key.id,application,candidates))return s.Error(nexus::Invalid,"Recovery plan validation failed; no files changed.");
        meta.application_id=ApplicationId(meta);
        auto [name,added]=names.emplace(Family(meta),std::string{});
        if(added)name->second=SafeLabel(title::GetCachedTitleName(meta.application_id));
        meta.name=name->second;
        for(size_t i=0;i<files.size();++i){
            auto [it,inserted]=unique.emplace(candidates[i].id,plan.files.size());
            if(inserted)plan.files.push_back(std::move(files[i]));
            // A shared ID must describe the same bytes for every owner.
            else if(plan.files[it->second].path!=files[i].path || Convert(plan.files[it->second].info).size!=candidates[i].size)
                return s.Error(nexus::Invalid,"Shared content metadata disagrees. No files changed.");
            meta.files.push_back(it->second);
        }
        ++plan.repair_records;plan.metadata.push_back(std::move(meta));
    }
    R_TRY(s.Check());
    plan.after=repair::Output(plan.before,original,merged);
    plan.ready=plan.repair_records && (plan.folder_exists || !plan.maps.titles.empty());
    plan.detail="Records needing missing index entries: "+std::to_string(plan.repair_records)+
        "\nAlready indexed (size checked): "+std::to_string(plan.intact)+
        "\nSkipped incomplete/conflicting records: "+std::to_string(plan.skipped)+
        "\nConflicts: "+std::to_string(plan.conflicts)+" / index capacity limits: "+std::to_string(plan.limits);
    plan.detail+="\n\nIncludes secondary Program NCAs in game collections. The collection keeps one application entry; all its recorded NCA files are checked for missing index entries.";
    if(!plan.ready)plan.detail+="\n\nNo safe index additions found. Already indexed does not prove a game can boot. Missing USB files, system metadata or tickets require a reinstall from your original backup.";
    else plan.detail+="\n\nVerify + repair checks affected USB files. A hash mismatch skips that game's planned repairs; other verified games continue. Review issues lists each mismatched NCA and its owner. Existing records/settings stay intact; originals are backed up. Restart afterwards. Missing files/tickets cannot be recreated.";
    return 0;
}
static Result ApplyUsbRecoveryInternal(ui::ProgressBox* p,RecoveryPlan& plan){
    R_UNLESS(plan.ready && !plan.published,nexus::Invalid);plan.ready=false;
    plan.detail="Recovery stopped; existing files were not replaced. Any staging directory is safe to keep.";
    Session s(p,plan);R_TRY(s.Open());bool exists{};R_TRY(s.FolderExists(exists));
    RepairIO io{s};
    if(plan.pending){
        p->SetProgressNote("Keep storage connected until finished.");
        p->SetOverallTransfer(500,10000,"Restoring library backup",true);
        R_UNLESS(exists,nexus::Invalid);
        if(!repair::Restore(io,plan.journal)){
            if(!plan.published)plan.detail="Restoration refused: the journal or indexes changed, or SD is unavailable. Keep all repair files for diagnosis. Nothing was overwritten.";
            return R_FAILED(io.rc)?io.rc:nexus::Invalid;
        }
        plan.detail="Original routing indexes restored. Other files and settings were preserved.\n\nFully power off and restart, then run Help & options > Repair USB library again to fill missing records.";
        p->SetOverallTransfer(10000,10000,"Backup restored",true);
        return 0;
    }
    if(exists!=plan.folder_exists || !repair::Match(io,plan.before))return s.Error(nexus::Invalid,"Indexes changed since the scan. Nothing was repaired; close other installers and scan again.");
    repair::Image pending;R_TRY(s.Read(repair::Pending,pending));R_UNLESS(!pending.exists,nexus::Invalid);
    for(const auto& m:plan.metadata){std::vector<u8> now;R_TRY(s.Raw(m.key,now));R_UNLESS(now==m.raw,nexus::Invalid);}
    std::vector<u8> buffer(1024*1024);
    keys::Keys header_keys{};
    // Optional: failure to derive the key never disables the ordinary hash.
    const bool have_header_key=R_SUCCEEDED(keys::parse_keys(header_keys,false));
    u64 total_bytes=0,checked_bytes=0;size_t file_number=0;
    for(const auto& f:plan.files){u64 size{};ncmContentInfoSizeToU64(&f.info,&size);R_UNLESS(size && total_bytes<=UINT64_MAX-size,nexus::Invalid);total_bytes+=size;}
    R_UNLESS(total_bytes,nexus::Invalid);
    plan.verification_started=true;
    for(auto& f:plan.files){
        bool valid{};R_TRY(s.SameSize(f,valid));R_UNLESS(valid,nexus::Invalid);
        // A native SD program must not be redirected by a recovery operation.
        if(f.info.content_type==NcmContentType_Program){bool native{};R_TRY(ncmContentStorageHas(&s.native,&native,&f.info.content_id));R_UNLESS(!native,nexus::Invalid);}
        u64 size{};ncmContentInfoSizeToU64(&f.info,&size);nca::ContentHash hash;
        hash.EnablePlaintextOrigin();
        const auto stage="Checking file "+std::to_string(++file_number)+" of "+std::to_string(plan.files.size());
        p->SetOverallTransfer(500+static_cast<s64>(9000.0*checked_bytes/total_bytes),10000,stage);
        for(u64 off=0;off<size;){
            R_TRY(s.Check());const auto count=std::min<u64>(buffer.size(),size-off);u64 got{};
            R_TRY(s.usb.Read(plan.generation,f.path,off,buffer.data(),count,got));R_UNLESS(got==count,nexus::Invalid);
            hash.Update(buffer.data(),count,have_header_key?header_keys.header_key:nullptr);off+=count;checked_bytes+=count;
            p->SetOverallTransfer(500+static_cast<s64>(9000.0*checked_bytes/total_bytes),10000,stage);
        }
        hash.Raw(f.actual_hash.data());
        f.mismatch=!hash.Matches(&f.info.content_id,16,&f.converted_hash,&f.plaintext_origin);
        if(f.converted_hash)++plan.converted_files;
        if(f.plaintext_origin)++plan.plaintext_files;
        if(f.mismatch)++plan.mismatch_files;
    }
    plan.verification_complete=true;
    CollectIssues(plan);
    // Rebuild exclusively from complete verified families. Never publish the
    // optimistic scan plan after dropping a bad file: that would route partial
    // games or retain bad shared IDs. Original live entries are left untouched.
    repair::Indexes original,merged;
    R_UNLESS(repair::Decode(plan.before,original),nexus::Invalid);merged=original;
    plan.maps={};
    for(const auto& m:plan.metadata){
        if(m.blocked)continue;
        std::vector<core::Content> content;
        for(auto i:m.files)content.push_back(Convert(plan.files[i].info));
        const bool application=m.key.type==NcmContentMetaType_Application;
        const auto result=repair::Merge(merged,m.key.id,application,content);
        R_UNLESS(result==repair::MergeResult::Changed || result==repair::MergeResult::Intact,nexus::Invalid);
        if(!plan.folder_exists)R_UNLESS(core::Add(plan.maps,m.key.id,application,content),nexus::Invalid);
        ++plan.verified_records;
    }
    plan.after=repair::Output(plan.before,original,merged);
    // Recheck committed metadata after potentially long content verification.
    for(const auto& m:plan.metadata){std::vector<u8> now;R_TRY(s.Raw(m.key,now));R_UNLESS(now==m.raw,nexus::Invalid);}
    R_TRY(s.Check());
    if(!plan.verified_records || (!plan.folder_exists && plan.maps.titles.empty())){
        plan.detail="No indexes repaired\n\nNo complete, independently verified game set was available for publication. Existing indexes and USB files were not changed. Review the mismatches below; modified files can also fail this check.";
        return 0;
    }
    if(plan.before==plan.after){
        plan.repair_complete=true;
        plan.detail="Verification finished. The selected game's available USB content was hash-checked; no routing index changes were needed.\n\nMismatched files: "+std::to_string(plan.mismatch_files)+
            "\nNo files, tickets, saves or unrelated games were changed. This does not clear Nintendo's corruption flag or prove launch compatibility.";
        return 0;
    }
    if(plan.folder_exists){
        p->SetOverallTransfer(9600,10000,"Repairing library entries",true);
        const repair::Journal journal{armGetSystemTick(),plan.before,plan.after};
        if(!repair::Publish(io,journal))return R_FAILED(io.rc)?io.rc:nexus::Invalid;
        plan.repair_complete=true;plan.repaired_records=plan.verified_records;
        plan.detail="USB index repair finished\n\nRecords repaired: "+std::to_string(plan.repaired_records)+
            "\nRecords skipped due to mismatches in their game: "+std::to_string(plan.blocked_records)+
            "\nExisting entries/settings preserved; originals backed up.\n\nFully power off and restart before launching or reinstalling games. This is not a guarantee that every game can boot.";
        return 0;
    }
    struct Publisher {
        Session& s;Result rc{};std::string stage;
        bool Ok(Result r){if(R_FAILED(r)){rc=r;return false;}return true;}
        bool Check(){return Ok(s.Check());}
        bool Missing(){bool exists{};return Ok(s.FolderExists(exists)) && !exists;}
        bool Begin(){char name[96];snprintf(name,sizeof(name),"/atmosphere/usb_content-recovery-%016llx",static_cast<unsigned long long>(armGetSystemTick()));stage=name;const NativePath path(stage.c_str());return Ok(fsFsCreateDirectory(&s.sd,path.value));}
        bool Write(const char* name,const core::Bytes& data){
            const NativePath path((stage+"/"+name).c_str());if(!Ok(fsFsCreateFile(&s.sd,path.value,data.size(),0)))return false;
            FsFile file{};if(!Ok(fsFsOpenFile(&s.sd,path.value,FsOpenMode_Write,&file)))return false;
            const auto write=fsFileWrite(&file,0,data.data(),data.size(),FsWriteOption_Flush);fsFileClose(&file);if(!Ok(write))return false;
            if(!Ok(fsFsOpenFile(&s.sd,path.value,FsOpenMode_Read,&file)))return false;
            core::Bytes check(data.size());u64 got{};s64 size{};const auto sz=fsFileGetSize(&file,&size);
            const auto read=fsFileRead(&file,0,check.data(),check.size(),FsReadOption_None,&got);fsFileClose(&file);
            return Ok(sz) && Ok(read) && size==static_cast<s64>(data.size()) && got==data.size() && check==data;
        }
        bool Promote(){
            const NativePath from(stage.c_str()),to(Folder);
            if(!Ok(fsFsCommit(&s.sd)) || !Check() || !Missing() || !Ok(fsFsRenameDirectory(&s.sd,from.value,to.value)))return false;
            s.plan.published=true; // must exit even if the post-rename flush fails
            return Ok(fsFsCommit(&s.sd));
        }
    } publisher{s,0,{}};
    p->SetOverallTransfer(9600,10000,"Repairing library entries",true);
    if(!core::Publish(publisher,plan.maps)){
        if(plan.published)plan.detail="Recovery folder was named, but SD commit failed. Stop here and fully restart before any further installs. Preserve the SD and logs for inspection; recovery is NOT verified.";
        return R_FAILED(publisher.rc)?publisher.rc:nexus::Invalid;
    }
    plan.repair_complete=true;plan.repaired_records=plan.verified_records;
    plan.detail="USB indexes rebuilt\n\nRecords repaired: "+std::to_string(plan.repaired_records)+
        "\nRecords skipped due to mismatches in their game: "+std::to_string(plan.blocked_records)+
        "\nUSB game files were not changed.\n\nFully power off and restart before launching or reinstalling games. Deleted compatibility settings were not recreated.";
    return 0;
}

namespace {
std::string BuildReport(const RecoveryPlan& plan,Result result){
    char rc[16];snprintf(rc,sizeof(rc),"0x%08X",result);
    std::string text=std::string("Nexus Manager ")+nexus::updates::Version+" D1 - USB index repair report\nResult: "+std::string(rc)+
        "\nVerification complete: "+(plan.verification_complete?"yes":"NO - interrupted")+
        "\nIndex publication complete: "+(plan.repair_complete?"yes":"no")+
        "\nPublication started: "+(plan.published?"yes":"no")+
        "\nRecords repaired: "+std::to_string(plan.repaired_records)+
        "\nPlanned records skipped (affected game families): "+std::to_string(plan.blocked_records)+
        "\nUnique mismatched NCAs: "+std::to_string(plan.mismatch_files)+
        "\nVerified cartridge/download header conversions: "+std::to_string(plan.converted_files)+
        "\nVerified plaintext-origin Control headers: "+std::to_string(plan.plaintext_files)+
        "\nAlready indexed records (size checked only, NOT hash verified): "+std::to_string(plan.intact)+
        "\nOther records skipped during scan: "+std::to_string(plan.skipped)+
        "\n\n"+plan.detail+"\n\n";
    if(plan.folder_exists && plan.repair_complete)
        text+="Original index backups are in SD:/atmosphere/usb_content-repair-* (completed.bin identifies a finished transaction).\n\n";
    text+="HASH MISMATCH DOES NOT PROVE CORRUPTION: modified files also differ. No USB files, tickets or saves were deleted.\n"
          "Keep this report and your original backups. Restart before further installs. Reinstall the affected base/update/DLC from a known-good original backup; do not delete individual NCAs or saves manually.\n"
          "Existing index entries are preserved, including entries for skipped games. Skipped means no new repairs for that game, not that it was uninstalled.\n"
          "There is NO permanent block/blacklist. This report is diagnostic only and is never read by installation or launch. A fresh scan resets all mismatch results and checks the current files again after reinstall.\n\n";
    for(size_t i=0;i<plan.issues.size();++i){
        const auto& issue=plan.issues[i];
        text+="Mismatch "+std::to_string(i+1)+"\nGame/app: "+issue.name+
            "\nApplication ID: "+(issue.application_id?TitleId(issue.application_id):"unavailable - not guessed")+
            "\nRecord: "+TitleId(issue.record_id)+" ("+issue.kind+", version "+std::to_string(issue.version)+")"+
            "\nNCA: "+issue.nca+"\nUSB path: "+issue.path+
            "\nExpected content ID (first 16 SHA-256 bytes): "+issue.expected+
            "\nObserved SHA-256: "+issue.actual+"\n\n";
    }
    for(const auto& m:plan.metadata)if(m.blocked)
        text+="Skipped record: "+TitleId(m.key.id)+" / "+RecordKind(m.key.type)+" / "+m.name+"\n";
    for(const auto& f:plan.files)if(f.converted_hash)
        text+="Verified header conversion (entire body checked): "+f.path+"\n";
    for(const auto& f:plan.files)if(f.plaintext_origin)
        text+="Verified plaintext-origin Control (entire body checked): "+f.path+"\n";
    if(plan.issues.empty())text+="No mismatches observed"+std::string(plan.verification_complete?".\n":" before the operation stopped. Unread files are NOT verified.\n");
    return text;
}
Result SaveReport(FsFileSystem& sd,const std::string& text,std::string& path){
    for(const char* dir:{"/switch","/switch/nexusmanager","/switch/nexusmanager/recovery-reports"}){
        const NativePath p(dir);FsDirEntryType type{};
        const auto rc=fsFsGetEntryType(&sd,p.value,&type);
        if(rc==MissingSd){R_TRY(fsFsCreateDirectory(&sd,p.value));}
        else {R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir,nexus::Invalid);}
    }
    char name[128];snprintf(name,sizeof(name),"/switch/nexusmanager/recovery-reports/repair-%016llx.txt",static_cast<unsigned long long>(armGetSystemTick()));
    const NativePath tmp((std::string(name)+".tmp").c_str()),final(name);
    // Create-new only, then readback and rename. Never overwrite a previous
    // report; do not leave a truncated file looking like a finished report.
    R_TRY(fsFsCreateFile(&sd,tmp.value,text.size(),0));FsFile f{};
    R_TRY(fsFsOpenFile(&sd,tmp.value,FsOpenMode_Write,&f));
    auto rc=fsFileWrite(&f,0,text.data(),text.size(),FsWriteOption_Flush);fsFileClose(&f);R_TRY(rc);
    R_TRY(fsFsOpenFile(&sd,tmp.value,FsOpenMode_Read,&f));
    std::vector<char> actual(text.size());u64 got{};s64 size{};
    rc=fsFileGetSize(&f,&size);
    if(R_SUCCEEDED(rc))rc=fsFileRead(&f,0,actual.data(),actual.size(),FsReadOption_None,&got);
    fsFileClose(&f);R_TRY(rc);
    R_UNLESS(size==static_cast<s64>(text.size()) && got==text.size() && !memcmp(actual.data(),text.data(),text.size()),nexus::Invalid);
    R_TRY(fsFsCommit(&sd));R_TRY(fsFsRenameFile(&sd,tmp.value,final.value));R_TRY(fsFsCommit(&sd));
    path="SD:"+std::string(name);return 0;
}
std::string ShortLabel(const std::string& text){
    if(text.size()<=96)return text;
    size_t n=96;while(n && (static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;
    return text.substr(0,n)+"...";
}
}
std::string RecoveryIssuePage(const RecoveryPlan& plan,size_t index){
    if(index>=plan.issues.size())return "No mismatch details available";
    const auto& i=plan.issues[index];
    return "Hash mismatch "+std::to_string(index+1)+" / "+std::to_string(plan.issues.size())+
        "\n\n"+ShortLabel(i.name)+
        "\nGame ID: "+(i.application_id?TitleId(i.application_id):"unavailable; see record ID")+
        "\n"+i.kind+" / record "+TitleId(i.record_id)+
        "\nNCA: "+i.nca+
        "\n\nThis game's planned repairs were skipped. Other verified games can recover.\n"
        "No permanent block. Restart, then reinstall the affected content from a known-good backup and scan again. Modified files also fail this check. Do not manually delete NCAs or saves.";
}
Result ApplyUsbRecovery(ui::ProgressBox* p,RecoveryPlan& plan){
    R_UNLESS(plan.ready && !plan.published,nexus::Invalid);
    const auto result=ApplyUsbRecoveryInternal(p,plan);
    if(!plan.verification_started)return result;
    if(R_SUCCEEDED(result))p->SetOverallTransfer(9800,10000,"Saving the report",true);
    // Include mismatches already seen even if a later I/O error interrupted
    // verification. I/O/short-read/cancellation never become skippable hashes.
    CollectIssues(plan);
    plan.report_text=BuildReport(plan,result);
    FsFileSystem sd{};auto saved=fsOpenSdCardFileSystem(&sd);
    if(R_SUCCEEDED(saved)){saved=SaveReport(sd,plan.report_text,plan.report_path);fsFsClose(&sd);}
    if(R_SUCCEEDED(saved))plan.detail+="\n\nFull report: "+plan.report_path;
    else plan.detail+="\n\nWARNING: report could not be saved. Photograph each Review issues page before exiting.";
    if(!plan.issues.empty())plan.detail+="\nReview issues: "+std::to_string(plan.mismatch_files)+" mismatched NCA(s). Nothing was deleted.";
    if(R_SUCCEEDED(result))p->SetOverallTransfer(10000,10000,"Check finished",true);
    return result;
}
Result ReadUsbRoutingContent(std::set<std::array<u8,16>>& ids){
    ids.clear();FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
    repair::Snapshot snapshot;
    for(size_t i=0;i<snapshot.size();++i){
        const NativePath path(repair::Live(i).c_str());FsFile file{};
        const auto rc=fsFsOpenFile(&sd,path.value,FsOpenMode_Read,&file);if(rc==MissingSd)continue;R_TRY(rc);
        s64 size{};auto result=fsFileGetSize(&file,&size);u64 actual{};
        if(R_SUCCEEDED(result)&&(size<0||size>static_cast<s64>(repair::MaxFile)))result=nexus::Invalid;
        if(R_SUCCEEDED(result)){snapshot[i].data.resize(size);result=fsFileRead(&file,0,snapshot[i].data.data(),size,FsReadOption_None,&actual);}
        fsFileClose(&file);R_TRY(result);R_UNLESS(actual==snapshot[i].data.size(),nexus::Invalid);snapshot[i].exists=true;
    }
    repair::Indexes indexes;R_UNLESS(repair::Decode(snapshot,indexes),nexus::Invalid);
    ids.insert(indexes.content.begin(),indexes.content.end());
    for(const auto& [title,mapping]:indexes.mappings)for(const auto& id:mapping)if(id!=std::array<u8,16>{})ids.insert(id);
    return 0;
}
Result UsbRoutingReferences(const std::set<std::array<u8,16>>& ids,bool& found){
    found=false;std::set<std::array<u8,16>> mapped;R_TRY(ReadUsbRoutingContent(mapped));
    for(const auto& id:ids)found|=mapped.contains(id);return 0;
}
Result RemoveDeletedUsbRouting(ui::ProgressBox* p,u64 generation,const std::vector<NcmContentId>& removed,
                              const std::vector<u64>& applications){
    RecoveryPlan plan;plan.generation=generation;Session s(p,plan);R_TRY(s.Open());R_TRY(s.FolderExists(plan.folder_exists));
    R_TRY(nexus::CheckUsbMutationAllowed());R_TRY(s.Snapshot(plan.before));
    repair::Indexes before,after;R_UNLESS(repair::Decode(plan.before,before),nexus::Invalid);after=before;
    for(const auto& id:removed){
        R_TRY(s.Check());nexus::Info info{};const auto path=CanonicalUsbContentPath(id);
        R_UNLESS(s.usb.Stat(generation,path,info)==MissingUsb,nexus::Invalid);
        core::Id value{};memcpy(value.data(),&id,16);after.content.erase(value);
        for(auto it=after.mappings.begin();it!=after.mappings.end();){
            if(std::find(it->second.begin(),it->second.end(),value)!=it->second.end()){after.titles.erase(it->first);it=after.mappings.erase(it);}else ++it;
        }
    }
    for(auto app:applications){after.titles.erase(app);after.mappings.erase(app);}
    if(before==after)return 0;
    R_UNLESS(plan.folder_exists,nexus::Invalid);plan.after=repair::Output(plan.before,before,after);
    RepairIO io{s};const repair::Journal journal{armGetSystemTick(),plan.before,plan.after};
    if(!repair::Publish(io,journal))return R_FAILED(io.rc)?io.rc:nexus::Invalid;
    return 0;
}
}
