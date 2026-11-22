#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/port_transfer.hpp"
#include "nexus/library.hpp"
#include "nexus/port_catalog.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "nacp_compat.hpp"
#include "owo.hpp"
#include "fs.hpp"
#include "image.hpp"
#include "utils/utils.hpp"
#include <memory>
#include <cstring>

namespace sphaira::nexusapp {
namespace {
using namespace nexus::port;
struct Hash {
    Sha256Context ctx{};
    Hash(){sha256ContextCreate(&ctx);}
    void Update(const void* data,size_t size){sha256ContextUpdate(&ctx,data,size);}
    Digest Final(){Digest out{};sha256ContextGetHash(&ctx,out.data());return out;}
};
class PortIO {
    ui::ProgressBox* p;const Plan& plan;fs::FsNativeSd sd;nexus::Client client;
    std::string stage,read_path,write_path;fs::File read_file,write_file;
    u64 copy_start{};Phase current_phase{Phase::Preparing};
    Result Poll(){R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll(),nexus::Unavailable);return 0;}
public:
    using Result=::Result;using Hash=nexusapp::Hash;
    static constexpr Result Invalid=nexus::Invalid,Conflict=MAKERESULT(2,2);
    PortIO(ui::ProgressBox* progress,const Plan& selected):p(progress),plan(selected){}
    Result Open(){
        R_TRY(sd.GetFsOpenResult());nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
        R_TRY(client.Check(plan.generation));NexusVsdStatus vsd{};R_TRY(client.VirtualSdStatus(vsd));
        R_UNLESS(!(vsd.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);
        // Keep the PC adapters out of the snapshot/copy/delete transaction.
        R_UNLESS(nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        return Check();
    }
    size_t BufferSize(){return std::min<size_t>(4*1024*1024,client.TransferLimit());}
    Result Check(){R_TRY(Poll());return client.Check(plan.generation);}
    void Progress(const nexus::port::Progress& progress){
        current_phase=progress.phase;
        const auto note=progress.Note();
        const auto transferred=progress.copied;
        if(transferred>copy_start)p->AddTransferBytes(transferred-copy_start);
        copy_start=transferred;
        p->SetTitle(plan.name);
        const auto stage_text=std::string(progress.Stage())+(progress.path.empty()?"":" - "+progress.path);
        p->SetDetailedProgress(progress.Percent(),stage_text,progress.Bytes(),note);
    }
    void EndRead(){read_file.Close();read_path.clear();}
    Result Stat(bool usb,const std::string& path,Entry& e,bool& exists){
        e={};exists=false;R_TRY(Check());
        if(usb){nexus::Info info{};const auto rc=client.Stat(plan.generation,path,info);
            if(rc==MAKERESULT(140,205))return 0;R_TRY(rc);e={path,info.directory?0:info.size,bool(info.directory)};
        }else{
            R_UNLESS(path.size()<FS_MAX_PATH,Invalid);FsDirEntryType type{};auto rc=sd.GetEntryType(path,&type);
            if(rc==MAKERESULT(2,1))return 0;R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir||type==FsDirEntryType_File,Invalid);
            e.path=path;e.directory=type==FsDirEntryType_Dir;
            if(!e.directory){fs::File f;R_TRY(sd.OpenFile(path,FsOpenMode_Read,&f));s64 size{};R_TRY(f.GetSize(&size));R_UNLESS(size>=0,Invalid);e.size=size;}
        }
        exists=true;return 0;
    }
    Result List(bool usb,const std::string& path,std::vector<std::string>& names){
        R_TRY(Check());names.clear();
        if(usb){std::vector<nexus::Entry> entries;R_TRY(client.List(plan.generation,path,entries));
            for(const auto& e:entries){R_UNLESS(memchr(e.name,0,sizeof(e.name)),Invalid);names.emplace_back(e.name);}
        }else{
            fs::Dir dir;R_TRY(sd.OpenDirectory(path,FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&dir));
            auto batch=std::make_unique<std::array<FsDirectoryEntry,16>>();
            for(;;){s64 count{};R_TRY(Check());R_TRY(dir.Read(&count,batch->size(),batch->data()));R_UNLESS(count>=0&&count<=16,Invalid);if(!count)break;
                for(s64 i=0;i<count;++i){const auto& e=(*batch)[i];R_UNLESS(memchr(e.name,0,sizeof(e.name))&&names.size()<131072,Invalid);names.emplace_back(e.name);}}
        }
        return 0;
    }
    Result Read(bool usb,const std::string& path,u64 off,void* data,size_t count,u64& actual){
        // The engine checks generation once per chunk; USB Read also checks it
        // atomically in the storage owner. Avoid a second status IPC here.
        R_TRY(Poll());if(usb){R_TRY(client.Read(plan.generation,path,off,data,count,actual));if(current_phase!=Phase::Copying)p->AddTransferBytes(actual);return 0;}
        if(read_path!=path || off==0){EndRead();R_TRY(sd.OpenFile(path,FsOpenMode_Read,&read_file));read_path=path;}
        R_TRY(read_file.Read(off,data,count,FsReadOption_None,&actual));if(current_phase!=Phase::Copying)p->AddTransferBytes(actual);return 0;
    }
    Result Directory(bool usb,const std::string& path){
        R_UNLESS(path.size()<256 && nexus_vsd::Canonical(path),Invalid);
        for(size_t end=path.find('/',1);;end=path.find('/',end+1)){
            const auto part=path.substr(0,end);Entry e;bool exists{};R_TRY(Stat(usb,part,e,exists));
            if(exists){R_UNLESS(e.directory,Conflict);}else{
                R_TRY(Check());if(usb){R_TRY(client.CreateDirectory(plan.generation,part));}else{R_TRY(sd.CreateDirectory(part));}
            }
            if(end==path.npos)break;
        }return 0;
    }
    Result Preflight(u64 required){
        R_TRY(Check());R_TRY(nexus::CheckUsbMutationAllowed());nexus::Space space{};
        if(plan.source_usb){R_TRY(nexus::QueryPhysicalSd(space));}else{R_TRY(client.GetSpace(plan.generation,space));}
        R_UNLESS(required<=space.free && space.free-required>=32*1024*1024,MAKERESULT(2,30));
        if(!plan.source_usb&&plan.create_launcher){NexusVsdStatus vsd{};R_TRY(client.VirtualSdStatus(vsd));
            nexus_vsd::Profile profile{};R_UNLESS(nexus_vsd::Parse(plan.Destination(plan.nro),profile),Invalid);
            R_UNLESS(!nexus_vsd::UsbRoot(profile)||(vsd.reserved&NEXUS_VSD_CAP_USB_ROOT),nexus::Unavailable);}
        return 0;
    }
    Result CreateTemporary(size_t index,u64 size,std::string& out){
        const bool usb=!plan.source_usb;
        if(stage.empty()){
            const std::string parent="/switch/nexusmanager/port-staging";R_TRY(Directory(usb,parent));
            std::array<u8,16> nonce{};randomGet(nonce.data(),nonce.size());stage=parent+"/"+HexId(nonce.data(),nonce.size());
            // CreateDirectory is exclusive. Never reuse a previous operation's stage.
            if(usb){R_TRY(client.CreateDirectory(plan.generation,stage));}else{R_TRY(sd.CreateDirectory(stage));}
        }
        out=stage+"/"+std::to_string(index);R_TRY(Check());
        if(usb)return client.CreateFile(plan.generation,out,size);
        return sd.CreateFile(out,size,FsCreateOption_BigFile);
    }
    Result WriteTemporary(const std::string& path,u64 off,const void* data,size_t count){
        R_UNLESS(!stage.empty()&&Within(path,stage),Invalid);R_TRY(Poll());
        if(!plan.source_usb)return client.Write(plan.generation,path,off,data,count);
        if(write_path.empty()){R_TRY(sd.OpenFile(path,FsOpenMode_Write,&write_file));write_path=path;}
        R_UNLESS(write_path==path,Invalid);
        return write_file.Write(off,data,count,FsWriteOption_None);
    }
    Result Flush(){
        R_TRY(Check());EndRead();
        if(!write_path.empty()){
            const auto rc=fsFileFlush(&write_file.m_native);
            write_file.Close();write_path.clear();R_TRY(rc);
        }
        // A staged SD file is flushed once, before rename/readback. USB writes
        // remain write-through and use the owner's explicit flush command.
        return plan.source_usb?0:client.Flush(plan.generation);
    }
    Result Publish(const std::string& from,const std::string& to){
        R_UNLESS(!stage.empty()&&Within(from,stage)&&Safe(to),Invalid);
        R_TRY(Directory(!plan.source_usb,to.substr(0,to.find_last_of('/'))));
        Entry e;bool exists{};R_TRY(Stat(!plan.source_usb,to,e,exists));R_UNLESS(!exists,Conflict);R_TRY(Check());
        return plan.source_usb?sd.RenameFile(from,to):client.RenameNew(plan.generation,from,to);
    }
    Result RemoveSource(const std::string& path,u64 size){
        R_UNLESS(Safe(path),Invalid);R_TRY(Check());R_TRY(nexus::CheckUsbMutationAllowed());
        Entry e;bool exists{};R_TRY(Stat(plan.source_usb,path,e,exists));R_UNLESS(exists&&!e.directory&&e.size==size,Invalid);
        return plan.source_usb?client.DeletePortFile(plan.generation,path,size):sd.DeleteFile(path);
    }
    Result InstallLauncher(){
        R_TRY(Check());const auto path=plan.Destination(plan.nro);const bool usb=!plan.source_usb;
        Entry e;bool exists{};R_TRY(Stat(usb,path,e,exists));R_UNLESS(exists&&!e.directory,Invalid);
        auto exact=[&](u64 off,void* data,size_t size)->Result{u64 got{};R_TRY(Read(usb,path,off,data,size,got));R_UNLESS(got==size,Invalid);return 0;};
        std::array<u8,128> header{};R_TRY(exact(0,header.data(),header.size()));nexus::library::Assets assets{};
        R_UNLESS(nexus::library::Header(header,e.size,assets),Invalid);
        OwoConfig cfg{};cfg.port_shortcut=true;cfg.nro_path="sdmc:"+plan.nro;cfg.args=plan.args;
        cfg.address_space=plan.address_space==1?ForwarderAddressSpace::Bit36:ForwarderAddressSpace::Bit39;
        cfg.name=plan.name+(usb?" (USB)":" (SD)");cfg.author="Nexus Manager / Port";
        cfg.nacp.supported_language_flag=0xffff;cfg.nacp.accessible_program_index_flags=1;std::strcpy(cfg.nacp.display_version,"1.0.0");
        std::array<u8,56> aset{};
        if(nexus::library::Range(assets.code,aset.size(),e.size)){
            R_TRY(exact(assets.code,aset.data(),aset.size()));
            if(nexus::library::AssetHeader(aset,e.size,assets)){
                if(assets.nacp_size)R_TRY(exact(assets.nacp,&cfg.nacp,sizeof(cfg.nacp)));
                if(assets.icon_size){cfg.icon.resize(assets.icon_size);R_TRY(exact(assets.icon,cfg.icon.data(),cfg.icon.size()));cfg.icon=ImageNormalizeIcon(cfg.icon);}
            }
        }
        if(cfg.icon.empty()){fs::FsStdio rom;R_TRY(rom.read_entire_file("romfs:/usb-loader-icon.jpg",cfg.icon));}
        if(usb){cfg.usb_path=path;Hash hash;std::vector<u8> buffer(BufferSize());
            for(u64 off=0;off<assets.code;){const auto n=std::min<u64>(buffer.size(),assets.code-off);R_TRY(exact(off,buffer.data(),n));hash.Update(buffer.data(),n);off+=n;}
            const auto digest=hash.Final();cfg.usb_nro_hash.assign(digest.begin(),digest.end());}
        // Both launchers live internally, independent of the port's data volume.
        R_TRY(Check());R_TRY(install_forwarder(p,cfg,NcmStorageId_BuiltInUser));return Check();
    }
};
}
Result PreparePortTransfer(ui::ProgressBox* p,const Game& selected,bool move,Plan& plan,std::string& detail){
    plan={};detail="Reading the installed forwarder's launch target"_nx;Game fresh;R_TRY(InspectGame(p,selected.id,fresh));
    R_UNLESS(fresh.forwarder_migratable&&!fresh.forwarder_path.empty(),nexus::Invalid);
    plan.application=selected.id;plan.name=selected.name;plan.move=move;plan.source_usb=fresh.usb_forwarder;plan.args=fresh.forwarder_args;
    if(plan.name.ends_with(" (USB)"))plan.name.resize(plan.name.size()-6);
    else if(plan.name.ends_with(" (SD)"))plan.name.resize(plan.name.size()-5);
    plan.nro=fresh.forwarder_path;if(plan.nro.starts_with("sdmc:"))plan.nro.erase(0,5);
    R_UNLESS(Safe(plan.nro)&&nexus_vsd::Nro(plan.nro),nexus::Invalid);
    if(plan.source_usb){nexus_vsd::Profile profile{};R_UNLESS(nexus_vsd::Parse(fresh.forwarder_usb_path,profile)&&plan.nro==profile.logical,nexus::Invalid);
        if(!nexus_vsd::UsbRoot(profile))plan.source_root=profile.root;
    }else{
        // Preserve normal /switch layouts. Other layouts use an isolated virtual
        // SD root, keeping absolute game data paths valid without touching SD.
        if(!plan.nro.starts_with("/switch/")){std::array<u8,32> hash{};const auto identity=plan.nro+"\n"+plan.args;
            sha256CalculateHash(hash.data(),identity.data(),identity.size());plan.destination_root="/homebrew/port-"+HexId(hash.data(),8)+"/sdmc";}
    }
    const auto root=DefaultRoot(plan.nro);R_UNLESS(!root.empty(),nexus::Invalid);plan.roots.push_back(root);
    plan.address_space=fresh.forwarder_address_space;
    if(const auto known=FindKnownPort(selected.id,plan.nro,plan.args)){
        if(known->blocked){detail="The catalogue reports a mismatched launch target for "_nx+std::string(known->name)+". Correct its forwarder before transferring this port."_nx;return nexus::Invalid;}
        R_UNLESS(known->nro==plan.nro&&known->args==plan.args,nexus::Invalid);
        plan.address_space=known->address;plan.roots.clear();
        for(size_t at=0;at<known->roots.size();){auto end=known->roots.find('\n',at);if(end==known->roots.npos)end=known->roots.size();
            plan.roots.emplace_back(known->roots.substr(at,end-at));at=end+1;}
        if(known->shared)plan.retained_roots=plan.roots;
    }
    nexus::Client c;nexus::Status status{};R_TRY(c.Query(status));R_UNLESS(status.Available(),nexus::Unavailable);plan.generation=status.generation;
    PortIO io(p,plan);R_TRY(io.Open());detail="Reading the complete port folder"_nx;R_TRY(Inventory(io,plan));
    detail=PortTransferPreview(plan);return 0;
}
std::string PortTransferPreview(const Plan& p){
    std::string text=p.name+"\n\n";
    for(const auto& root:p.roots)text+=(p.source_usb?"usb:":"sdmc:")+p.Source(root)+"\n  -> "+(p.source_usb?"sdmc:":"usb:")+p.Destination(root)+"\n";
    if(!p.retained_roots.empty())text+="\nShared runtime: source files will be kept for other ports.\n"_nx;
    text+="\n"+utils::formatSizeStorage(p.bytes)+" — all files and subfolders included.\n"_nx;
    text+=p.create_launcher?"A new HOME shortcut will launch the destination copy.\n\n"_nx:"No forwarder is required. HOME shortcuts are managed separately.\n\n"_nx;
    text+=p.move?"Source files are removed only after the destination is verified."_nx:"The original folder is kept."_nx;
    return text;
}
Result InspectPortFiles(ui::ProgressBox* p,Plan& plan,std::string& detail){
    R_UNLESS(!plan.application&&!plan.create_launcher&&!plan.roots.empty(),nexus::Invalid);
    PortIO io(p,plan);R_TRY(io.Open());detail="Reading the complete port folders"_nx;R_TRY(Inventory(io,plan));
    detail=PortTransferPreview(plan);return 0;
}
Result TransferPort(ui::ProgressBox* p,const Plan& plan,Report& report){
    report={};PortIO io(p,plan);R_TRY(io.Open());
    // A preview cannot authorize a changed launcher target.
    if(plan.application){Game current;R_TRY(InspectGame(p,plan.application,current));
    std::string path=current.forwarder_path;if(path.starts_with("sdmc:"))path.erase(0,5);
    R_UNLESS(current.forwarder_migratable && current.usb_forwarder==plan.source_usb && path==plan.nro && current.forwarder_args==plan.args &&
        (!plan.source_usb||current.forwarder_usb_path==plan.Source(plan.nro)),nexus::Invalid);
    }else R_UNLESS(!plan.create_launcher,nexus::Invalid);
    const auto rc=Execute(io,plan,report);nexus::Log("port-transfer",rc,report.removed);
    return rc;
}
}
