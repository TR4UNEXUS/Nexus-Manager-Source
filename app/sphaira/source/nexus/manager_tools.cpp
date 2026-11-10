// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/manager_tools.hpp"
#include "nexus/manager_backup.hpp"
#include "utils/utils.hpp"
#include "title_info.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <set>
#include <map>
#include <charconv>
#include "nexus/transfer_policy.hpp"
#include "nexus/transfer.hpp"
#include "nexus/lifecycle.hpp"

namespace sphaira::nexusmanager {
namespace {
constexpr u64 MaxBackup=1ULL<<40;
constexpr size_t MaxEntries=32768,BufferSize=1024*1024;
using nexus::Invalid;
struct Hash {
    Sha256Context ctx{};
    Hash(){sha256ContextCreate(&ctx);}
    void Update(const void* p,size_t n){sha256ContextUpdate(&ctx,p,n);}
    std::array<u8,32> Final(){std::array<u8,32> out{};sha256ContextGetHash(&ctx,out.data());return out;}
};
std::string Id(u64 value){char s[17];snprintf(s,sizeof(s),"%016llX",static_cast<unsigned long long>(value));return s;}
bool Supported(const FsSaveDataInfo& i){return i.save_data_space_id==FsSaveDataSpaceId_User &&
    (i.save_data_type==FsSaveDataType_Account || i.save_data_type==FsSaveDataType_Device) &&
    i.save_data_rank==FsSaveDataRank_Primary && i.application_id;}
FsSaveDataAttribute Attribute(const FsSaveDataInfo& i){
    FsSaveDataAttribute a{};a.application_id=i.application_id;a.uid=i.uid;a.system_save_data_id=i.system_save_data_id;
    a.save_data_type=i.save_data_type;a.save_data_rank=i.save_data_rank;a.save_data_index=i.save_data_index;return a;
}
Result Extra(const FsSaveDataInfo& info,FsSaveDataExtraData& out){
    return fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&out,sizeof(out),FsSaveDataSpaceId_User,info.save_data_id);
}
bool SameSave(const FsSaveDataExtraData& extra,const FsSaveDataInfo& info){
    const auto a=Attribute(info);const auto& b=extra.attr;
    return a.application_id==b.application_id && !memcmp(&a.uid,&b.uid,sizeof(a.uid)) &&
        a.system_save_data_id==b.system_save_data_id && a.save_data_type==b.save_data_type &&
        a.save_data_rank==b.save_data_rank && a.save_data_index==b.save_data_index;
}
struct Planned {std::string path;u64 size{};bool dir{};auto operator<=>(const Planned&) const=default;};
Result Tree(ui::ProgressBox* p,fs::Fs& source,const std::string& root,std::vector<Planned>& entries,u64& total){
    // Iterative traversal: do not multiply a 12-KiB directory buffer by depth
    // on the protected worker's 128-KiB stack. At most one directory is open.
    std::vector<std::pair<std::string,unsigned>> pending{{root,0}};
    auto batch=std::make_unique<std::array<FsDirectoryEntry,16>>();
    for(size_t at=0;at<pending.size();++at){
        const auto [folder,depth]=pending[at];
        R_TRY(p->ShouldExitResult());R_UNLESS(depth<64,Invalid);
        fs::Dir dir;R_TRY(source.OpenDirectory(folder,FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&dir));
        for(;;){
            s64 count{};R_TRY(p->ShouldExitResult());R_TRY(dir.Read(&count,batch->size(),batch->data()));
            R_UNLESS(count>=0 && size_t(count)<=batch->size(),Invalid);if(!count)break;
            for(s64 i=0;i<count;++i){
                const auto& e=(*batch)[i];R_UNLESS(memchr(e.name,0,sizeof(e.name)) && e.file_size>=0,Invalid);
                const auto child=folder=="/"?folder+e.name:folder+"/"+e.name;
                R_UNLESS(nexus::manager::SafeRelative(child.substr(1)) && entries.size()<MaxEntries,Invalid);
                R_UNLESS(e.type==FsDirEntryType_Dir || e.type==FsDirEntryType_File,Invalid);
                const bool is_dir=e.type==FsDirEntryType_Dir;
                const u64 size=is_dir?0:static_cast<u64>(e.file_size);
                R_UNLESS(size<=MaxBackup-total,Invalid);total+=size;
                entries.push_back({child,size,is_dir});
                if(is_dir)pending.push_back({child,depth+1});
            }
        }
    }
    return 0;
}
struct Copier {
    using Result=::Result;using Hash=nexusmanager::Hash;static constexpr Result Invalid=nexus::Invalid;
    fs::File& source;fs::File& dest;ui::ProgressBox* progress;u64& done;u64 total;
    std::array<u8,32>* digest{};
    bool require_foreground{};
    void RecordDigest(const std::array<u8,32>& hash){if(digest)*digest=hash;}
    Result Cancelled(){R_TRY(progress->ShouldExitResult());return require_foreground && !nexus::lifecycle::CanPoll()?nexus::Unavailable:0;}
    Result ReadSource(u64 off,void* data,size_t n,u64& actual){return source.Read(off,data,n,0,&actual);}
    Result WriteDestination(u64 off,const void* data,size_t n){return dest.Write(off,data,n,0);}
    Result ReadDestination(u64 off,void* data,size_t n,u64& actual){return dest.Read(off,data,n,0,&actual);}
    Result FlushDestination(){return fsFileFlush(&dest.m_native);}
    void Advance(size_t n){done+=n;progress->SetOverallTransfer(done,std::max<u64>(total,1),"Copying and verifying save backup");}
};
Result WriteNew(fs::Fs& fs,const std::string& path,const std::string& text){
    R_TRY(fs.CreateFile(path,text.size()));fs::File file;
    R_TRY(fs.OpenFile(path,FsOpenMode_Write,&file));R_TRY(file.Write(0,text.data(),text.size(),FsWriteOption_Flush));return 0;
}
}
std::string Display(std::string text){for(auto& c:text)if(static_cast<unsigned char>(c)<32 || c==127)c='?';return text;}
std::string SaveDetails(const Save& s){
    return Display(s.name)+"\n\nApplication: "+Id(s.info.application_id)+"\nSave: "+Id(s.info.save_data_id)+
        "\nType: "+(s.info.save_data_type==FsSaveDataType_Account?"User save":"Device save")+
        "\nUser: "+Id(s.info.uid.uid[1])+Id(s.info.uid.uid[0])+"\nAllocated size: "+utils::formatSizeStorage(s.info.size);
}
Result ListSaves(ui::ProgressBox* p,std::vector<Save>& result){
    result.clear();FsSaveDataInfoReader reader{};R_TRY(fsOpenSaveDataInfoReader(&reader,FsSaveDataSpaceId_User));
    struct Guard{FsSaveDataInfoReader* p;~Guard(){fsSaveDataInfoReaderClose(p);}} guard{&reader};
    std::array<FsSaveDataInfo,16> batch{};s64 count{};size_t examined{};
    for(;;){
        R_TRY(p->ShouldExitResult());R_TRY(fsSaveDataInfoReaderRead(&reader,batch.data(),batch.size(),&count));
        R_UNLESS(count>=0 && size_t(count)<=batch.size() && examined+count<=MaxEntries,Invalid);if(!count)break;examined+=count;
        for(s64 i=0;i<count;++i)if(Supported(batch[i])){
            auto name=title::GetCachedTitleName(batch[i].application_id);
            if(name.empty())name="Game "+Id(batch[i].application_id);
            result.push_back({batch[i],Display(name)});
        }
    }
    std::sort(result.begin(),result.end(),[](const Save& a,const Save& b){return a.name==b.name?a.info.save_data_id<b.info.save_data_id:a.name<b.name;});
    return 0;
}
Result BackupSave(ui::ProgressBox* p,const Save& save,std::string& detail,std::string* completed_path){
    if(completed_path)completed_path->clear();
    detail="Save backup did not finish. The original save was not changed.";
    R_UNLESS(Supported(save.info),Invalid);R_TRY(p->ShouldExitResult());
    nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
    // Require identity and commit metadata rather than guessing when unavailable.
    FsSaveDataExtraData before{},after{};R_TRY(Extra(save.info,before));R_UNLESS(SameSave(before,save.info),Invalid);
    FsFileSystem raw{};const auto attr=Attribute(save.info);
    R_TRY(fsOpenReadOnlySaveDataFileSystem(&raw,FsSaveDataSpaceId_User,&attr));
    fs::FsNative source(&raw,true,false);fs::FsNativeSd dest;R_TRY(dest.GetFsOpenResult());
    std::vector<Planned> plan;u64 total{};R_TRY(Tree(p,source,"/",plan,total));
    // FAT destinations are case-insensitive. Reject aliasing instead of overwriting.
    std::set<std::string> names;
    for(const auto& e:plan){auto folded=e.path;for(char& c:folded)if(c>='A' && c<='Z')c+=32;R_UNLESS(names.insert(folded).second,Invalid);}
    s64 free{};R_TRY(dest.GetFreeSpace("/",&free));
    // Includes deliberately conservative cluster and manifest headroom.
    const u64 needed=total+(plan.size()+4)*4ULL*1024*1024;
    R_UNLESS(free>=0 && u64(free)>=needed,nexus::Unavailable);
    const std::string root="/switch/nexusmanager/save-backups";
    R_TRY(dest.CreateDirectoryRecursively(root));
    const auto final=root+"/"+Id(save.info.application_id)+"-"+Id(save.info.save_data_id)+"-"+Id(armGetSystemTick());
    const auto stage=final+".partial";
    R_TRY(dest.CreateDirectory(stage)); // exclusive: no existing backup is reused
    detail="Backup incomplete. Original save unchanged. Keep SD:"+stage+" for inspection; it is NOT a completed backup.";
    R_TRY(dest.CreateDirectory(stage+"/data"));
    auto buffer=std::make_unique<u8[]>(BufferSize);u64 done{};std::string checksums;
    p->SetProgressNote("Read-only source; complete copy plus SHA-256 readback");
    for(const auto& e:plan){
        R_TRY(p->ShouldExitResult());const auto dst=stage+"/data"+e.path;
        R_UNLESS(dst.size()<FS_MAX_PATH,Invalid);
        if(e.dir){R_TRY(dest.CreateDirectory(dst));continue;}
        p->SetTitle(Display(e.path));
        fs::File in,out;R_TRY(source.OpenFile(e.path,FsOpenMode_Read,&in));
        s64 size{};R_TRY(in.GetSize(&size));R_UNLESS(size>=0 && u64(size)==e.size,Invalid);
        R_TRY(dest.CreateFile(dst,e.size,e.size>=0x100000000ULL?FsCreateOption_BigFile:0));
        R_TRY(dest.OpenFile(dst,FsOpenMode_Read|FsOpenMode_Write,&out));
        std::array<u8,32> digest{};Copier io{in,out,p,done,total*2,&digest};R_TRY(nexus::manager::CopyVerified(io,e.size,buffer.get(),BufferSize));
        checksums+="sha256="+nexusapp::HexId(digest.data(),digest.size())+" "+e.path.substr(1)+"\n";
        R_TRY(in.GetSize(&size));R_UNLESS(size>=0 && u64(size)==e.size,Invalid);
        R_TRY(out.GetSize(&size));R_UNLESS(size>=0 && u64(size)==e.size,Invalid);
    }
    std::vector<Planned> final_plan;u64 final_size{};R_TRY(Tree(p,source,"/",final_plan,final_size));
    std::sort(plan.begin(),plan.end());std::sort(final_plan.begin(),final_plan.end());
    R_UNLESS(plan==final_plan && total==final_size,Invalid);
    R_TRY(Extra(save.info,after));R_UNLESS(SameSave(after,save.info) && before.commit_id==after.commit_id,Invalid);
    const auto manifest="NEXUS-SAVE-BACKUP 2\napplication="+Id(save.info.application_id)+"\nsave="+Id(save.info.save_data_id)+
        "\nuser="+Id(save.info.uid.uid[1])+Id(save.info.uid.uid[0])+"\ntype="+std::to_string(save.info.save_data_type)+
        "\nindex="+std::to_string(save.info.save_data_index)+"\ncommit="+Id(before.commit_id)+"\nbytes="+std::to_string(total)+
        "\nentries="+std::to_string(plan.size())+"\nverified=sha256-copy-readback\nformat=plain-files-in-data; NOT DBI format\n"+checksums;
    R_TRY(WriteNew(dest,stage+"/backup.txt",manifest));R_TRY(dest.Commit());R_TRY(p->ShouldExitResult());
    // The final name is the completion marker; partial folders never become valid
    // because of size alone. Rename does not replace an existing destination.
    R_TRY(dest.RenameDirectory(stage,final));
    detail="Backup files verified, but final SD commit was not confirmed. Original save unchanged. Inspect SD:"+final;
    R_TRY(dest.Commit());
    p->SetOverallTransfer(1,1,"Save backup complete",true);
    detail="Save backup complete\n\nEvery copied file passed SHA-256 readback; the source save commit stayed unchanged. Original save kept.\n\nSD:"+final+
        "\n\nKeep the whole folder on your PC. Nexus restores version 2 backups to the same game/user; this is not a DBI backup.";
    if(completed_path)*completed_path=final;
    return 0;
}
namespace {
struct BackupPlan {std::vector<Planned> files;std::map<std::string,std::string> hashes;u64 total{};};
bool BackupFolder(const std::string& path){
    for(const auto* root:{"/switch/nexusmanager/save-backups/","/switch/nexusmanager/save-imports/"})if(path.starts_with(root)){
        const auto name=path.substr(std::strlen(root));return nexus::manager::SafeRelative(name) && name.find('/')==name.npos && !nexus::transfer::IsPartial(name);
    }return false;
}
Result Digest(ui::ProgressBox* p,fs::Fs& fs,const std::string& path,u64 expected,std::string& result){
    fs::File f;R_TRY(fs.OpenFile(path,FsOpenMode_Read,&f));s64 size{};R_TRY(f.GetSize(&size));R_UNLESS(size>=0 && u64(size)==expected,Invalid);
    auto buffer=std::make_unique<u8[]>(BufferSize);Hash h;
    for(u64 off=0;off<expected;){R_TRY(p->ShouldExitResult());const auto n=std::min<u64>(BufferSize,expected-off);u64 actual{};R_TRY(f.Read(off,buffer.get(),n,0,&actual));R_UNLESS(actual==n,Invalid);h.Update(buffer.get(),n);off+=n;}
    R_TRY(f.GetSize(&size));R_UNLESS(size>=0 && u64(size)==expected,Invalid);auto digest=h.Final();result=nexusapp::HexId(digest.data(),digest.size());return 0;
}
Result ReadBackup(ui::ProgressBox* p,fs::Fs& sd,const Save& save,const std::string& folder,BackupPlan& plan,bool hash){
    R_UNLESS(BackupFolder(folder) && Supported(save.info),Invalid);
    fs::File f;R_TRY(sd.OpenFile(folder+"/backup.txt",FsOpenMode_Read,&f));s64 size{};R_TRY(f.GetSize(&size));R_UNLESS(size>0 && size<=32*1024*1024,Invalid);
    std::string text(size,'\0');u64 actual{};R_TRY(f.Read(0,text.data(),text.size(),0,&actual));R_UNLESS(actual==text.size() && text.starts_with("NEXUS-SAVE-BACKUP 2\n") && text.back()=='\n',Invalid);
    std::map<std::string,std::string> fields;size_t offset=text.find('\n')+1;
    while(offset<text.size()){
        const auto end=text.find('\n',offset);R_UNLESS(end!=text.npos,Invalid);const auto line=text.substr(offset,end-offset);offset=end+1;
        const auto split=line.find('=');R_UNLESS(split!=line.npos,Invalid);auto key=line.substr(0,split),value=line.substr(split+1);
        if(key=="sha256"){
            R_UNLESS(value.size()>65 && value[64]==' ' && plan.hashes.size()<MaxEntries,Invalid);
            auto digest=value.substr(0,64),path=value.substr(65);R_UNLESS(nexus::manager::SafeRelative(path),Invalid);
            for(char& c:digest){if(c>='A'&&c<='F')c+=32;R_UNLESS((c>='0'&&c<='9')||(c>='a'&&c<='f'),Invalid);}
            R_UNLESS(plan.hashes.emplace("/"+path,digest).second,Invalid);
        }else R_UNLESS(fields.size()<16 && fields.emplace(key,value).second,Invalid);
    }
    R_UNLESS(fields["application"]==Id(save.info.application_id) && fields["save"]==Id(save.info.save_data_id) &&
        fields["user"]==Id(save.info.uid.uid[1])+Id(save.info.uid.uid[0]) && fields["type"]==std::to_string(save.info.save_data_type) && fields["index"]==std::to_string(save.info.save_data_index),Invalid);
    R_TRY(Tree(p,sd,folder+"/data",plan.files,plan.total));
    R_UNLESS(fields["bytes"]==std::to_string(plan.total) && fields["entries"]==std::to_string(plan.files.size()),Invalid);
    std::set<std::string> names;size_t count{};
    for(auto& e:plan.files){
        e.path=e.path.substr((folder+"/data").size());std::string folded=e.path;
        R_UNLESS(nexus::manager::SafeRelative(e.path.substr(1)),Invalid);
        for(char& c:folded)if(c>='A'&&c<='Z')c+=32;
        R_UNLESS(names.insert(folded).second,Invalid);
        if(e.dir)continue;
        ++count;const auto it=plan.hashes.find(e.path);R_UNLESS(it!=plan.hashes.end(),Invalid);
        if(hash){p->SetTitle(Display(e.path));std::string digest;R_TRY(Digest(p,sd,folder+"/data"+e.path,e.size,digest));R_UNLESS(digest==it->second,Invalid);}
    }
    R_UNLESS(count==plan.hashes.size(),Invalid);return 0;
}
}
Result ListSaveBackups(ui::ProgressBox* p,const Save& save,std::vector<std::string>& result){
    R_UNLESS(nexus::transfer::Active()==nexus::transfer::Mode::Off,Invalid);result.clear();nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));fs::FsNativeSd sd;R_TRY(sd.GetFsOpenResult());
    for(const auto* root:{"/switch/nexusmanager/save-backups","/switch/nexusmanager/save-imports"}){
        if(!sd.DirExists(root))continue;
        fs::Dir dir;R_TRY(sd.OpenDirectory(root,FsDirOpenMode_ReadDirs,&dir));
        auto e=std::make_unique<FsDirectoryEntry>();size_t examined{};
        for(;;){R_TRY(p->ShouldExitResult());s64 count{};R_TRY(dir.Read(&count,1,e.get()));if(!count)break;R_UNLESS(++examined<=4096 && result.size()<256,Invalid);
            if(e->type!=FsDirEntryType_Dir || !memchr(e->name,0,sizeof(e->name)))continue;
            const auto folder=std::string(root)+"/"+e->name;BackupPlan plan;
            if(R_SUCCEEDED(ReadBackup(p,sd,save,folder,plan,false)))result.push_back(folder);
        }
    }std::sort(result.begin(),result.end());return p->ShouldExitResult();
}
Result RestoreSave(ui::ProgressBox* p,const Save& save,const std::string& folder,std::string& detail){
    detail="Restore stopped before changing the save. Use a complete Nexus version 2 backup for this same game/user. Disconnect PC transfer first.";
    R_UNLESS(nexus::transfer::Active()==nexus::transfer::Mode::Off && Supported(save.info),Invalid);
    nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));fs::FsNativeSd sd;R_TRY(sd.GetFsOpenResult());
    BackupPlan plan;R_TRY(ReadBackup(p,sd,save,folder,plan,true));
    FsSaveDataExtraData before{},current{};R_TRY(Extra(save.info,before));R_UNLESS(SameSave(before,save.info) && before.journal_size>0,Invalid);
    std::string recovery;R_TRY(BackupSave(p,save,detail,&recovery));R_UNLESS(!recovery.empty(),Invalid);
    detail="Restore did not finish. A verified recovery backup is kept at SD:"+recovery+". Do not delete it. Close the game and retry if needed.";
    R_TRY(Extra(save.info,current));R_UNLESS(SameSave(current,save.info) && current.commit_id==before.commit_id,Invalid);
    // Horizon's save filesystem owns the transaction. Do not commit partial
    // replacement or a cancellation; closing the uncommitted session discards it.
    FsFileSystem raw{};const auto attr=Attribute(save.info);R_TRY(fsOpenSaveDataFileSystem(&raw,FsSaveDataSpaceId_User,&attr));
    fs::FsNative dest(&raw,true,false);
    R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll(),nexus::Unavailable);R_TRY(fsFsCleanDirectoryRecursively(&dest.m_fs,"/"));
    auto buffer=std::make_unique<u8[]>(BufferSize);u64 done{};
    p->SetProgressNote("Restoring and verifying; current save backed up first");
    for(const auto& e:plan.files){
        R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll(),nexus::Unavailable);p->SetTitle(Display(e.path));
        if(e.dir){R_TRY(dest.CreateDirectory(e.path));continue;}
        fs::File in,out;R_TRY(sd.OpenFile(folder+"/data"+e.path,FsOpenMode_Read,&in));R_TRY(dest.CreateFile(e.path,e.size));R_TRY(dest.OpenFile(e.path,FsOpenMode_Read|FsOpenMode_Write,&out));
        std::array<u8,32> digest{};Copier io{in,out,p,done,plan.total*2,&digest,true};R_TRY(nexus::manager::CopyVerified(io,e.size,buffer.get(),BufferSize));
        R_UNLESS(nexusapp::HexId(digest.data(),digest.size())==plan.hashes.at(e.path),Invalid);
    }
    std::vector<Planned> actual;u64 bytes{};R_TRY(Tree(p,dest,"/",actual,bytes));std::sort(actual.begin(),actual.end());std::sort(plan.files.begin(),plan.files.end());
    R_UNLESS(actual==plan.files && bytes==plan.total,Invalid);R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll(),nexus::Unavailable);
    // Commit exactly once, after every file passed source and readback hashes.
    R_TRY(dest.Commit());
    detail="Save restored and verified.\n\nRecovery backup kept at SD:"+recovery+"\n\nThe uploaded backup is also kept.";return 0;
}
Result InspectFile(ui::ProgressBox* p,bool usb,u64 gen,const nexusapp::Item& item,bool hash,std::string& detail){
    detail="File inspection stopped. No files were changed.";
    R_UNLESS(!item.directory && nexus::VolumePathValid(item.path),Invalid);
    nexus::Client client;fs::FsNativeSd sd;fs::File file;
    auto size=[&](u64& out)->Result{
        if(usb){nexus::Info info{};R_TRY(client.Stat(gen,item.path,info));R_UNLESS(!info.directory,Invalid);out=info.size;}
        else {s64 n{};R_TRY(file.GetSize(&n));R_UNLESS(n>=0,Invalid);out=n;}return 0;
    };
    if(!usb){nexus::Space physical{};R_TRY(nexus::QueryPhysicalSd(physical));R_TRY(sd.GetFsOpenResult());R_TRY(sd.OpenFile(item.path,FsOpenMode_Read,&file));}
    u64 bytes{};R_TRY(size(bytes));R_UNLESS(bytes==item.size && bytes<=INT64_MAX,Invalid);
    const u64 limit=hash?bytes:std::min<u64>(bytes,256);
    auto buffer=std::make_unique<u8[]>(BufferSize);Hash digest;std::string preview;
    for(u64 offset=0;offset<limit;){
        R_TRY(p->ShouldExitResult());const auto count=std::min<u64>(BufferSize,limit-offset);u64 actual{};
        R_TRY(usb?client.Read(gen,item.path,offset,buffer.get(),count,actual):file.Read(offset,buffer.get(),count,0,&actual));
        R_UNLESS(actual==count,Invalid);
        if(hash)digest.Update(buffer.get(),count);
        else for(size_t i=0;i<count;++i){
            if(i%16==0){char label[12];snprintf(label,sizeof(label),"%04X  ",unsigned(i));preview+=label;}
            char pair[4];snprintf(pair,sizeof(pair),"%02X ",buffer[i]);preview+=pair;if(i%16==15)preview+='\n';
        }
        offset+=count;p->SetOverallTransfer(offset,std::max<u64>(limit,1),hash?"Calculating SHA-256":"Reading preview");
    }
    u64 after{};R_TRY(size(after));R_UNLESS(bytes==after,Invalid);if(usb)R_TRY(client.Check(gen));
    detail=Display(item.name)+"\nSize: "+utils::formatSizeStorage(bytes)+"\n\n";
    if(hash){auto sum=digest.Final();detail+="SHA-256\n"+nexusapp::HexId(sum.data(),16)+"\n"+nexusapp::HexId(sum.data()+16,16)+
        "\n\nCompare with a trusted checksum. A digest alone does not prove a game is intact. Close other apps that could change this file while hashing.";}
    else detail+="First "+std::to_string(limit)+" bytes (hex)\n"+preview+"\nRead-only preview; nothing changed.";
    return 0;
}
}
