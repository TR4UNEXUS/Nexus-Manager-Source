#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/file_manager.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "app.hpp"
#include <cstring>
#include <memory>
namespace sphaira::nexusfiles {
namespace {
using namespace nexus::files;
struct Hash {
    Sha256Context ctx{};Hash(){sha256ContextCreate(&ctx);}
    void Update(const void* p,size_t n){sha256ContextUpdate(&ctx,p,n);}
    Digest Final(){Digest d{};sha256ContextGetHash(&ctx,d.data());return d;}
};
class IO {
public:
    using Result=::Result;using Hash=nexusfiles::Hash;
    static constexpr Result Invalid=nexus::Invalid,Conflict=MAKERESULT(2,2);
private:
    ui::ProgressBox* progress{};bool source_usb{},destination_usb{},destination_used{};
    u64 source_generation{},destination_generation{},usb_generation{},sd_generation{};
    bool sd_open{},device_open{},vsd_available{};FsFileSystem sd{};FsDeviceOperator device{};
    std::array<u8,16> cid{};nexus::Client client;
    u64 work_total{},work_done{};
    FsFile read_file{},write_file{};bool read_open{},write_open{};std::string read_path,write_path,stage,final,reviewed_root,progress_path,progress_action;u64 progress_done{};
    template<class F> ::Result SdPath(const std::string& path,F&& call){
        std::array<char,FS_MAX_PATH> buffer{};R_UNLESS(Canonical(path)&&path.size()<buffer.size(),nexus::Invalid);
        memcpy(buffer.data(),path.data(),path.size());return call(buffer.data());
    }
    bool RuntimeSafe(const std::string& path)const {
        if(envIsNso())return true;
        auto active=Fold(std::string(App::GetExePath()));
        if(active.starts_with("sdmc:/"))active.erase(0,5);
        if(!Canonical(active))return false;
        return !Within(active,Fold(path)) && !Within(Fold(path),active);
    }
    Result Mutation(bool usb){R_TRY(Check());return usb?nexus::CheckUsbMutationAllowed():0;}
    bool Owned(const std::string& path)const{return !stage.empty()&&Canonical(path)&&Within(path,stage);}
public:
    IO(ui::ProgressBox* p,bool from_usb,u64 from_generation,bool to_usb=false,u64 to_generation=0,bool to_used=false,
       std::string root={},std::string destination={}):progress(p),source_usb(from_usb),destination_usb(to_usb),destination_used(to_used),
       source_generation(from_generation),destination_generation(to_generation),final(std::move(destination)),reviewed_root(std::move(root)){}
    ~IO(){EndRead();if(write_open)fsFileClose(&write_file);if(sd_open)fsFsClose(&sd);if(device_open)fsDeviceOperatorClose(&device);}
    Result Open(bool browsing=false){
        if(!reviewed_root.empty())R_UNLESS(RuntimeSafe(reviewed_root),Invalid);
        if(!final.empty())R_UNLESS(RuntimeSafe(final),Invalid);
        if(source_usb||(destination_used&&destination_usb)){
            usb_generation=source_usb?source_generation:destination_generation;
            if(source_usb&&destination_used&&destination_usb)R_UNLESS(source_generation==destination_generation,Invalid);
            R_TRY(client.Check(usb_generation));
        }
        Result provider_rc{};const auto provider=nexus::Detect(provider_rc);
        if(provider==nexus::Provider::Unknown)return R_FAILED(provider_rc)?provider_rc:nexus::Unavailable;
        if(provider==nexus::Provider::Present){NexusVsdStatus vsd{};R_TRY(client.VirtualSdStatus(vsd));vsd_available=true;
            R_UNLESS(!(vsd.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}
        else R_UNLESS(!usb_generation,nexus::Unavailable);
        if(!source_usb||(destination_used&&!destination_usb)){
            nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
            // Own a freshly opened physical filesystem, not the process's
            // possibly redirected sdmc devoptab; never reopen after failure.
            R_TRY(fsOpenSdCardFileSystem(&sd));sd_open=true;
            R_TRY(fsOpenDeviceOperator(&device));device_open=true;
            R_TRY(fsDeviceOperatorGetSdCardCid(&device,cid.data(),cid.size(),cid.size()));
            Digest id{};sha256CalculateHash(id.data(),cid.data(),cid.size());memcpy(&sd_generation,id.data(),sizeof(sd_generation));
            if(!sd_generation)sd_generation=1;
            if(!browsing){
                if(!source_usb)R_UNLESS(source_generation==sd_generation,Invalid);
                if(destination_used&&!destination_usb)R_UNLESS(destination_generation==sd_generation,Invalid);
            }
        }return Check();
    }
    u64 Generation(bool usb)const{return usb?usb_generation:sd_generation;}
    Result Check(){
        if(progress)R_TRY(progress->ShouldExitResult());
        R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        if(usb_generation)R_TRY(client.Check(usb_generation));
        if(vsd_available){NexusVsdStatus vsd{};R_TRY(client.VirtualSdStatus(vsd));R_UNLESS(!(vsd.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}
        else {Result rc{};R_UNLESS(nexus::Detect(rc)==nexus::Provider::Absent,R_FAILED(rc)?rc:nexus::Unavailable);}
        if(sd_open){bool inserted{};R_TRY(fsDeviceOperatorIsSdCardInserted(&device,&inserted));R_UNLESS(inserted,nexus::Unavailable);
            std::array<u8,16> current{};R_TRY(fsDeviceOperatorGetSdCardCid(&device,current.data(),current.size(),current.size()));R_UNLESS(current==cid,nexus::Unavailable);}
        return 0;
    }
    size_t BufferSize()const{return std::min<size_t>(1024*1024,usb_generation?client.TransferLimit():1024*1024);}
    void BeginWork(u64 total){work_total=std::max<u64>(total,1);work_done=0;if(progress)progress->SetDetailedProgress(0,"Preparing files"_nx,{},"Overall task progress"_nx);}
    void ReadProgress(u64 count){if(progress)progress->AddTransferBytes(count);if(!work_total||!progress)return;work_done=std::min(work_total,work_done+count);
        progress->SetDetailedProgress(nexus::WorkPart(work_done,work_total,98),progress_action+" / "+progress_path,{},"Overall task progress"_nx);}
    void Progress(const char* action,const std::string& path,u64 done,u64 total){
        if(!progress)return;
        if(progress_path!=path||progress_action!=action||done<progress_done){progress_path=path;progress_action=action;progress->NewTransfer(path);}
        progress_done=done;
        progress->SetTitle(path).SetActionName(progress_action=="Removing"?"Removing"_nx:action);
        if(progress_action=="Removing"&&!work_total)progress->SetOverallTransfer(done,std::max<u64>(total,1),"Deleting / "_nx+path,true).SetProgressNote(std::to_string(done)+" of "_nx+std::to_string(total)+" items removed"_nx);
        else if(!work_total)progress->NewTransfer(path);
    }
    void EndRead(){if(read_open)fsFileClose(&read_file);read_open=false;read_path.clear();}
    Result Stat(bool usb,const std::string& path,Entry& e,bool& exists){
        e={};exists=false;R_UNLESS(Canonical(path),Invalid);R_TRY(Check());
        if(usb){nexus::Info info{};const auto rc=client.Stat(usb_generation,path,info);if(rc==MAKERESULT(140,205))return 0;R_TRY(rc);
            R_UNLESS(info.directory<=1,Invalid);e.size=info.directory?0:info.size;e.directory=info.directory;
        }else{R_UNLESS(sd_open,Invalid);FsDirEntryType type{};const auto rc=SdPath(path,[&](const char* native){return fsFsGetEntryType(&sd,native,&type);});if(rc==MAKERESULT(2,1))return 0;R_TRY(rc);
            R_UNLESS(type==FsDirEntryType_Dir||type==FsDirEntryType_File,Invalid);e.directory=type==FsDirEntryType_Dir;
            if(!e.directory){FsFile file{};R_TRY(SdPath(path,[&](const char* native){return fsFsOpenFile(&sd,native,FsOpenMode_Read,&file);}));s64 size{};const auto r=fsFileGetSize(&file,&size);fsFileClose(&file);R_TRY(r);R_UNLESS(size>=0,Invalid);e.size=size;}}
        e.path=path;exists=true;return 0;
    }
    Result List(bool usb,const std::string& path,std::vector<std::string>& names){
        names.clear();R_UNLESS(Canonical(path),Invalid);R_TRY(Check());
        if(usb){std::vector<nexus::Entry> entries;R_TRY(client.List(usb_generation,path,entries));R_UNLESS(entries.size()<=MaxEntries,Invalid);
            for(const auto& e:entries){R_UNLESS(memchr(e.name,0,sizeof(e.name)),Invalid);names.emplace_back(e.name);}}
        else{R_UNLESS(sd_open,Invalid);FsDir dir{};R_TRY(SdPath(path,[&](const char* native){return fsFsOpenDirectory(&sd,native,FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&dir);}));
            struct Close {FsDir* d;~Close(){fsDirClose(d);}} close{&dir};auto batch=std::make_unique<std::array<FsDirectoryEntry,16>>();
            for(;;){R_TRY(Check());s64 count{};R_TRY(fsDirRead(&dir,&count,batch->size(),batch->data()));R_UNLESS(count>=0&&count<=16,Invalid);if(!count)break;
                R_UNLESS(static_cast<size_t>(count)<=MaxEntries-names.size(),Invalid);
                for(s64 i=0;i<count;++i){const auto& e=(*batch)[i];R_UNLESS(memchr(e.name,0,sizeof(e.name)),Invalid);names.emplace_back(e.name);}}}
        return 0;
    }
    Result Read(bool usb,const std::string& path,u64 off,void* data,size_t size,u64& actual){
        actual=0;R_TRY(Check());R_UNLESS(Canonical(path)&&size<=UINT64_MAX-off,Invalid);
        if(usb){R_TRY(client.Read(usb_generation,path,off,data,size,actual));}
        else {R_UNLESS(sd_open,Invalid);if(!read_open||read_path!=path||off==0){EndRead();R_TRY(SdPath(path,[&](const char* native){return fsFsOpenFile(&sd,native,FsOpenMode_Read,&read_file);}));read_open=true;read_path=path;}
            R_TRY(fsFileRead(&read_file,off,data,size,FsReadOption_None,&actual));}
        ReadProgress(actual);return 0;
    }
    Result Preflight(u64 bytes){
        R_TRY(Mutation(destination_usb));nexus::Space space{};
        if(destination_usb){R_TRY(client.GetSpace(usb_generation,space));}else{s64 available{};R_TRY(SdPath("/",[&](const char* native){return fsFsGetFreeSpace(&sd,native,&available);}));R_UNLESS(available>=0,Invalid);space.free=available;}
        R_UNLESS(bytes<=space.free&&space.free-bytes>=32*1024*1024,MAKERESULT(2,30));return 0;
    }
    Result NewStage(bool directory,u64 size,std::string& out){
        R_UNLESS(destination_used&&Safe(final),Invalid);std::array<u8,16> nonce{};randomGet(nonce.data(),nonce.size());
        stage=Child(Parent(final),std::string(StagePrefix)+nexusapp::HexId(nonce.data(),nonce.size()));R_UNLESS(StagePath(stage),Invalid);
        Entry e;bool exists{};R_TRY(Stat(destination_usb,stage,e,exists));R_UNLESS(!exists,Conflict);
        // On a failed IPC reply the creation may have happened; retain the
        // attempted name for explicit inspection instead of deleting blindly.
        out=stage;return directory?Directory(destination_usb,stage):Create(destination_usb,stage,size);
    }
    Result Directory(bool usb,const std::string& path){R_UNLESS(usb==destination_usb&&Owned(path),Invalid);R_TRY(Mutation(usb));
        return usb?client.CreateDirectory(usb_generation,path):SdPath(path,[&](const char* native){return fsFsCreateDirectory(&sd,native);});}
    Result Create(bool usb,const std::string& path,u64 size){R_UNLESS(usb==destination_usb&&Owned(path)&&size<=MaxBytes,Invalid);R_TRY(Mutation(usb));
        return usb?client.CreateFile(usb_generation,path,size):SdPath(path,[&](const char* native){return fsFsCreateFile(&sd,native,size,size>=0x100000000ULL?FsCreateOption_BigFile:0);});}
    Result Write(bool usb,const std::string& path,u64 off,const void* data,size_t size){R_UNLESS(usb==destination_usb&&Owned(path)&&size<=UINT64_MAX-off,Invalid);R_TRY(Mutation(usb));
        if(usb)return client.Write(usb_generation,path,off,data,size);
        if(!write_open){R_TRY(SdPath(path,[&](const char* native){return fsFsOpenFile(&sd,native,FsOpenMode_Write,&write_file);}));write_open=true;write_path=path;}
        R_UNLESS(write_path==path,Invalid);return fsFileWrite(&write_file,off,data,size,FsWriteOption_None);
    }
    Result Flush(bool usb){R_TRY(Mutation(usb));EndRead();
        if(write_open){const auto rc=fsFileFlush(&write_file);fsFileClose(&write_file);write_open=false;write_path.clear();R_TRY(rc);}
        return usb?client.Flush(usb_generation):fsFsCommit(&sd);
    }
    Result Publish(const std::string& from,const std::string& to,bool directory){
        R_UNLESS(from==stage&&to==final&&Safe(to)&&RuntimeSafe(to),Invalid);R_TRY(Mutation(destination_usb));
        Entry e;bool exists{};R_TRY(Stat(destination_usb,to,e,exists));R_UNLESS(!exists,Conflict);
        if(destination_usb)return client.RenameNew(usb_generation,from,to);
        return SdPath(from,[&](const char* a){return SdPath(to,[&](const char* b){return directory?fsFsRenameDirectory(&sd,a,b):fsFsRenameFile(&sd,a,b);});});
    }
    Result Remove(bool usb,const std::string& path,u64 size,bool directory){
        Snapshot review;review.root=reviewed_root;review.staging=StagePath(reviewed_root);
        R_UNLESS(usb==source_usb&&Within(path,reviewed_root)&&SnapshotPath(review,path)&&RuntimeSafe(path),Invalid);R_TRY(Mutation(usb));
        Entry e;bool exists{};R_TRY(Stat(usb,path,e,exists));R_UNLESS(exists&&e.directory==directory&&e.size==size,Invalid);
        if(directory){std::vector<std::string> names;R_TRY(List(usb,path,names));R_UNLESS(names.empty(),Invalid);}
        if(usb)return client.DeleteFileManagerEntry(usb_generation,path,size,directory,reviewed_root);
        return SdPath(path,[&](const char* native){return directory?fsFsDeleteDirectory(&sd,native):fsFsDeleteFile(&sd,native);});
    }
};
std::string Location(bool usb,const std::string& path){return std::string(usb?"USB:":"SD:")+path;}
std::string Counts(const Snapshot& s){const auto dirs=std::count_if(s.entries.begin(),s.entries.end(),[](const Entry& e){return e.directory;});
    return std::to_string(s.entries.size()-dirs)+" files, "+std::to_string(dirs)+" folders\n"+std::to_string(s.bytes)+" bytes";}
void Failure(Report& r,bool move,bool deleting,const std::string& destination){
    const auto stage=r.detail;r.detail=(deleting?"Delete stopped. "_nx:"Transfer stopped. "_nx)+stage+".\n\n";
    if(r.removal_started)r.detail+="Source removal began; some items may already be gone, even if the last request reported an error. Do not retry using the old clipboard selection.\n"_nx;
    else r.detail+="No source deletion was attempted. The original items were kept.\n"_nx;
    if(r.published)r.detail+="The verified destination was published at "_nx+destination+".\n";
    else if(!deleting)r.detail+="A complete destination was not confirmed. Inspect "_nx+destination+" before using it.\n"_nx;
    if(!r.staged_path.empty())r.detail+="A temporary item may remain at "_nx+Location(destination.starts_with("USB:"),r.staged_path)+". Select that item in File manager and choose Delete to review and remove it.\n";
    r.detail+="Nothing was silently overwritten."_nx;
}
}
Result ListFiles(ui::ProgressBox* p,bool usb,const std::string& path,std::vector<nexusapp::Item>& items,u64& generation){
    // Browsing never mutates: show protected storage and retained staging
    // normally; the reviewed mutation entry points enforce their own policy.
    items.clear();generation=0;R_UNLESS(Canonical(path),nexus::Invalid);
    if(usb){nexus::Client client;nexus::Status s{};R_TRY(client.Query(s));R_UNLESS(s.Available(),nexus::Unavailable);generation=s.generation;}
    IO io(p,usb,generation);R_TRY(io.Open(true));generation=io.Generation(usb);R_TRY(CheckSpelling(io,usb,path));
    std::vector<std::string> names;R_TRY(io.List(usb,path,names));
    for(const auto& name:names){const auto full=Child(path,name);if(!Canonical(full))continue;Entry e;bool exists{};R_TRY(io.Stat(usb,full,e,exists));R_UNLESS(exists,nexus::Invalid);items.push_back({name,full,e.size,e.directory});}
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.directory!=b.directory?a.directory>b.directory:Fold(a.name)<Fold(b.name);});return io.Check();
}
std::string TransferPreview(const Plan& p){return std::string(p.move?"Cut and paste"_nx:"Copy and paste"_nx)+"\n\nFrom: "_nx+Location(p.source.usb,p.source.item.path)+
    "\nTo: "+Location(p.destination_usb,p.final_path)+"\n\n"+Counts(p.snapshot)+"\n\nAn existing destination will never be replaced. "_nx+
    (p.move?"Source items are removed only after the copy is verified. A cancellation or disconnect during removal may leave some source items behind."_nx:"The source items will be kept."_nx);}
std::string DeletePreview(const DeletePlan& p){return "Delete permanently?\n\n"_nx+Location(p.source.usb,p.source.item.path)+"\n\n"+Counts(p.snapshot)+
    "\n\nOnly these reviewed items will be removed. There is no recycle bin. Cancellation or a disconnect can leave a partially deleted folder."_nx;}
Result PrepareTransfer(ui::ProgressBox* p,const Selection& selection,bool destination_usb,u64 destination_generation,const std::string& directory,bool move,Plan& plan,std::string& detail){
    plan={};detail="The item or destination is protected, has an ambiguous name, or the storage changed. Refresh File manager and choose ordinary files or folders."_nx;
    R_UNLESS(Safe(selection.item.path)&&SafeDirectory(directory),nexus::Invalid);
    plan.source=selection;plan.destination_usb=destination_usb;plan.destination_generation=destination_generation;plan.destination_directory=directory;plan.move=move;
    plan.final_path=Child(directory,Leaf(selection.item.path));
    IO io(p,selection.usb,selection.generation,destination_usb,destination_generation,true,selection.item.path,plan.final_path);R_TRY(io.Open());
    R_TRY(CheckSpelling(io,destination_usb,directory));Entry parent;bool parent_exists{};R_TRY(io.Stat(destination_usb,directory,parent,parent_exists));R_UNLESS(parent_exists&&parent.directory,nexus::Invalid);
    Entry existing;bool exists{};R_TRY(io.Stat(destination_usb,plan.final_path,existing,exists));if(exists){detail="The destination already contains "_nx+Leaf(plan.final_path)+". Choose another folder; nothing will be overwritten."_nx;return IO::Conflict;}
    detail="Reading and checking the selected files. Nothing has been changed."_nx;R_TRY(Prepare(io,selection.usb,selection.item.path,plan.snapshot));
    R_UNLESS(plan.snapshot.entries[0].directory==selection.item.directory&&(!selection.item.directory?plan.snapshot.entries[0].size==selection.item.size:true),nexus::Invalid);
    if(!ValidDestination(selection.usb,destination_usb,plan.snapshot,plan.final_path)){detail="Cannot paste into the same item, into its own folder tree, or into a protected/overlong path."_nx;return nexus::Invalid;}
    R_TRY(io.Preflight(plan.snapshot.bytes));plan.prepared=true;detail=TransferPreview(plan);return 0;
}
Result Transfer(ui::ProgressBox* p,const Plan& plan,Report& report){
    report={};report.detail="The prepared selection is no longer valid"_nx;
    auto run=[&]()->Result{R_UNLESS(plan.prepared&&plan.snapshot.root==plan.source.item.path&&plan.final_path==Child(plan.destination_directory,Leaf(plan.source.item.path)),nexus::Invalid);
        IO io(p,plan.source.usb,plan.source.generation,plan.destination_usb,plan.destination_generation,true,plan.source.item.path,plan.final_path);R_TRY(io.Open());
        return nexus::files::Transfer(io,plan.source.usb,plan.destination_usb,plan.snapshot,plan.final_path,plan.move,report);};
    const auto rc=run();if(R_FAILED(rc))Failure(report,plan.move,false,Location(plan.destination_usb,plan.final_path));return rc;
}
Result PrepareDelete(ui::ProgressBox* p,const Selection& selection,DeletePlan& plan,std::string& detail){
    plan={};detail="This item is protected, has an ambiguous name, or the storage changed. Refresh File manager before trying again."_nx;
    const bool stage=StagePath(selection.item.path);R_UNLESS(Safe(selection.item.path)||stage,nexus::Invalid);
    IO io(p,selection.usb,selection.generation,false,0,false,selection.item.path);R_TRY(io.Open());
    detail="Listing the selected files and folders. File contents are not read."_nx;R_TRY(Inventory(io,selection.usb,selection.item.path,plan.snapshot,stage));
    R_UNLESS(plan.snapshot.entries[0].directory==selection.item.directory&&(!selection.item.directory?plan.snapshot.entries[0].size==selection.item.size:true),nexus::Invalid);
    plan.source=selection;plan.prepared=true;detail=DeletePreview(plan);return 0;
}
Result Delete(ui::ProgressBox* p,const DeletePlan& plan,Report& report){
    report={};report.detail="The prepared deletion is no longer valid"_nx;
    auto run=[&]()->Result{R_UNLESS(plan.prepared&&plan.snapshot.root==plan.source.item.path,nexus::Invalid);
        IO io(p,plan.source.usb,plan.source.generation,false,0,false,plan.source.item.path);R_TRY(io.Open());return nexus::files::QuickDelete(io,plan.source.usb,plan.snapshot,report);};
    const auto rc=run();if(R_FAILED(rc))Failure(report,false,true,{});return rc;
}
}
