// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/dump.hpp"
#include "nexus/dump_stream.hpp"
#include "nexus/manager.hpp"
#include "nexus/lifecycle.hpp"
#include "yati/container/nsp.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ns.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace sphaira::nexusapp {
namespace {
constexpr Result Invalid = nexus::Invalid;
constexpr u64 Reserve = 32ULL*1024*1024;
struct Part {
    std::string name;
    NcmContentId id{};
    u64 size{}, offset{};
    bool nca{};
    std::vector<u8> data;
};
struct Package {
    std::vector<Part> parts;
    std::vector<u8> header;
    u64 size{};
};

// USB uses the existing owner service, never another usb:hs session/mount.
// An exclusively created staging directory owns data.nsp even if allocating
// that file fails. Final names are never replaced; reconnects fail by generation.
class BackupOutput {
    nexus::Target target{nexus::Target::Sd};
    nexus::Client usb;
    std::unique_ptr<fs::FsNativeSd> sd;
    std::unique_ptr<fs::File> file;
    std::string partial, data;
public:
    u64 generation{};
    Result Open(nexus::Target t) {
        target=t;
        if(t==nexus::Target::Usb){
            R_TRY(nexus::CheckUsbMutationAllowed());
            nexus::Status status{};R_TRY(usb.Query(status));
            R_UNLESS(status.Available(),nexus::Unavailable);generation=status.generation;
            return usb.Check(generation);
        }
        R_UNLESS(t==nexus::Target::Sd,Invalid);
        sd=std::make_unique<fs::FsNativeSd>();return sd->GetFsOpenResult();
    }
    Result Space(u64& out) {
        if(target==nexus::Target::Usb){nexus::Space space{};R_TRY(usb.GetSpace(generation,space));out=space.free;return 0;}
        s64 free{};R_TRY(sd->GetFreeSpace("/",&free));R_UNLESS(free>=0,Invalid);out=free;return 0;
    }
    Result Stat(const std::string& path,bool& directory) {
        if(target==nexus::Target::Usb){
            nexus::Info info{};const auto rc=usb.Stat(generation,path,info);directory=info.directory;
            return rc==MAKERESULT(140,205)?FsError_PathNotFound:rc;
        }
        FsDirEntryType type{};const auto rc=sd->GetEntryType(fs::FsPath{path},&type);directory=type==FsDirEntryType_Dir;return rc;
    }
    Result Directories() {
        if(target==nexus::Target::Sd)return sd->CreateDirectoryRecursively("/dumps/NSP");
        for(const char* path:{"/dumps","/dumps/NSP"}){
            bool directory{};const auto rc=Stat(path,directory);
            if(rc==FsError_PathNotFound){R_TRY(usb.CreateDirectory(generation,path));}
            else {R_TRY(rc);R_UNLESS(directory,Invalid);}
        }
        R_SUCCEED();
    }
    Result Create(const std::string& path,u64 size,bool& owned) {
        partial=path;
        if(target==nexus::Target::Usb){
            // mkdir is exclusive in C21 (FatFs f_mkdir), unlike CreateFile.
            R_TRY(usb.CreateDirectory(generation,partial));owned=true;
            data=partial+"/data.nsp";
            return usb.CreateFile(generation,data,size);
        }
        R_TRY(sd->CreateFile(fs::FsPath{partial},size));owned=true;return 0;
    }
    Result OpenWrite() {
        if(target==nexus::Target::Usb)return usb.Check(generation);
        file=std::make_unique<fs::File>();return sd->OpenFile(fs::FsPath{partial},FsOpenMode_Write,file.get());
    }
    Result Write(u64 off,const void* src,u64 size) {
        return target==nexus::Target::Usb?usb.Write(generation,data,off,src,size):file->Write(off,src,size,FsWriteOption_None);
    }
    Result FlushWrite() {
        if(target==nexus::Target::Usb)return usb.Flush(generation);
        return fsFileFlush(&file->m_native);
    }
    void Close(){file.reset();}
    Result Commit(){return target==nexus::Target::Usb?usb.Flush(generation):sd->Commit();}
    Result OpenRead(u64 expected) {
        if(target==nexus::Target::Usb){
            nexus::Info info{};R_TRY(usb.Stat(generation,data,info));
            R_UNLESS(!info.directory && info.size==expected,Invalid);return 0;
        }
        file=std::make_unique<fs::File>();R_TRY(sd->OpenFile(fs::FsPath{partial},FsOpenMode_Read,file.get()));
        s64 size{};R_TRY(file->GetSize(&size));R_UNLESS(size>=0 && static_cast<u64>(size)==expected,Invalid);return 0;
    }
    Result Read(u64 off,void* dst,u64 size,u64& actual) {
        return target==nexus::Target::Usb?usb.Read(generation,data,off,dst,size,actual):file->Read(off,dst,size,FsReadOption_None,&actual);
    }
    Result Rename(const std::string& final_path) {
        if(target==nexus::Target::Usb)return usb.RenameNew(generation,data,final_path);
        return sd->RenameFile(fs::FsPath{partial},fs::FsPath{final_path});
    }
    Result Cleanup() {
        Close();
        if(target==nexus::Target::Sd)return sd->DeleteFile(fs::FsPath{partial});
        bool directory{};const auto rc=Stat(data,directory);
        if(R_SUCCEEDED(rc)){R_UNLESS(!directory,Invalid);R_TRY(usb.DeleteDumpTemporary(generation,data));}
        else R_UNLESS(rc==FsError_PathNotFound,rc);
        R_TRY(usb.DeleteDumpTemporary(generation,partial));return usb.Flush(generation);
    }
};

// Export only original common licenses from ES, with strict allocation/length
// bounds. Personalized tickets need a separately audited read-only save reader;
// do not mount BIS, invent licenses or silently emit an incomplete package.
Result OriginalLicense(const FsRightsId& id,std::vector<u8>& ticket,std::vector<u8>& cert) {
    u64 ts{},cs{};R_TRY(es::GetCommonTicketAndCertificateSize(&ts,&cs,&id));
    R_UNLESS(ts>=sizeof(es::TicketHmac160) && ts<=64*1024 && cs && cs<=64*1024,Invalid);
    ticket.resize(ts);cert.resize(cs);
    u64 actual_t{},actual_c{};
    R_TRY(es::GetCommonTicketAndCertificateData(&actual_t,&actual_c,ticket.data(),ts,cert.data(),cs,&id));
    R_UNLESS(actual_t==ts && actual_c==cs,Invalid);
    es::TicketData parsed{};R_TRY(es::GetTicketData(ticket,&parsed));
    R_UNLESS(!std::memcmp(&parsed.rights_id,&id,sizeof(id)) && parsed.title_key_type==es::TitleKeyType_Common,Invalid);
    R_SUCCEED();
}

Result BuildPackage(ui::ProgressBox* p,u64 app,nexus::Target source,NcmContentStorage& cs,u64 gen,Package& out,DumpReport& report) {
    const auto cancelled=[&]()->Result{return p?p->ShouldExitResult():(nexus::lifecycle::CanPoll()?0:Invalid);};
    report.stage="Checking installed content";
    std::vector<DumpRecord> records;R_TRY(CollectDumpRecords(app,source,gen,records));
    keys::Keys keys{};R_TRY(keys::parse_keys(keys,true));
    std::map<std::string,NcmContentInfo> unique;
    for(const auto& r:records){
        bool has_meta{};
        for(const auto& info:r.contents){
            R_TRY(cancelled());
            has_meta |= info.content_type==NcmContentType_Meta;
            const auto id=HexId(&info.content_id);
            const auto [it,inserted]=unique.emplace(id,info);
            if(!inserted){
                u64 a{},b{};ncmContentInfoSizeToU64(&info,&a);ncmContentInfoSizeToU64(&it->second,&b);
                R_UNLESS(a==b && info.content_type==it->second.content_type,Invalid);
            }
        }
        R_UNLESS(has_meta,Invalid);
    }
    R_UNLESS(!unique.empty() && unique.size()<=16384,Invalid);
    std::set<std::string> licenses;
    // Keep the familiar NCA-before-CNMT order. Content bytes are never patched.
    for(bool meta:{false,true})for(const auto& [id,info]:unique){
        if((info.content_type==NcmContentType_Meta)!=meta)continue;
        R_TRY(cancelled());report.content=id;
        report.stage="Reading game headers";if(p)p->NewTransfer(report.stage).UpdateTransfer(out.parts.size(),unique.size());
        u64 size{};ncmContentInfoSizeToU64(&info,&size);
        R_UNLESS(size>=sizeof(nca::Header),Invalid);
        nca::Header header{};
        R_TRY(ReadContent(source,cs,info.content_id,gen,0,&header,sizeof(header)));
        crypto::cryptoAes128Xts(&header,&header,keys.header_key,0,0x200,sizeof(header),false);
        R_UNLESS(header.IsValid() && header.size==size,Invalid);
        out.parts.push_back({id+(meta?".cnmt.nca":".nca"),info.content_id,size,0,true,{}});
        if(es::IsRightsIdValid(header.rights_id))licenses.insert(HexId(&header.rights_id));
    }
    if(!licenses.empty()){
        report.stage="Reading original licenses";
        R_TRY(es::Initialize());ON_SCOPE_EXIT(es::Exit());
        u64 license_bytes{};
        for(const auto& id:licenses){
            R_TRY(cancelled());
            FsRightsId rights{};keys::parse_hex_key(&rights,id.c_str());
            std::vector<u8> ticket,cert;
            const auto rc=OriginalLicense(rights,ticket,cert);
            if(R_FAILED(rc)){
                report.detail="The original common license could not be read. Personalized or missing licenses are not supported by this dump version. No license was changed or created.";
                return rc;
            }
            license_bytes+=ticket.size()+cert.size();
            R_UNLESS(license_bytes<=16*1024*1024,Invalid);
            out.parts.push_back({id+".tik",{},ticket.size(),0,false,std::move(ticket)});
            out.parts.push_back({id+".cert",{},cert.size(),0,false,std::move(cert)});
        }
    }
    report.content.clear();
    std::vector<yati::container::CollectionEntry> entries;
    u64 payload{};
    for(auto& part:out.parts){
        part.offset=payload;
        R_UNLESS(part.size && nexus::dump::AddSize(payload,part.size),Invalid);
        entries.push_back({part.name,static_cast<s64>(part.offset),static_cast<s64>(part.size)});
    }
    // Build is shared with the existing NSP exporter. Bound signed arithmetic
    // before calling it, including more than its maximum possible header size.
    u64 bounded=payload;R_UNLESS(nexus::dump::AddSize(bounded,entries.size()*128ULL+64),Invalid);
    s64 size{};out.header=yati::container::Nsp::Build(entries,size);
    R_UNLESS(size>0 && static_cast<u64>(size)==payload+out.header.size(),Invalid);
    out.size=size;
    R_SUCCEED();
}

Result ReadPackage(const Package& pkg,nexus::Target source,NcmContentStorage& cs,u64 gen,u64 off,void* data,u64 size,u64& actual) {
    actual=0;R_UNLESS(off<=pkg.size && size<=pkg.size-off,Invalid);
    auto dst=static_cast<u8*>(data);
    while(actual<size){
        const u64 pos=off+actual;
        if(pos<pkg.header.size()){
            const auto count=std::min<u64>(size-actual,pkg.header.size()-pos);
            std::memcpy(dst+actual,pkg.header.data()+pos,count);actual+=count;continue;
        }
        const auto rel=pos-pkg.header.size();
        const auto it=std::upper_bound(pkg.parts.begin(),pkg.parts.end(),rel,[](u64 n,const Part& part){return n<part.offset;});
        R_UNLESS(it!=pkg.parts.begin(),Invalid);
        const auto& part=*std::prev(it);
        R_UNLESS(rel>=part.offset && rel-part.offset<part.size,Invalid);
        const auto local=rel-part.offset,count=std::min<u64>(size-actual,part.size-local);
        if(part.nca){R_TRY(ReadContent(source,cs,part.id,gen,local,dst+actual,count));}
        else {R_UNLESS(part.data.size()==part.size,Invalid);std::memcpy(dst+actual,part.data.data()+local,count);}
        actual+=count;
    }
    R_SUCCEED();
}
}

struct VirtualNsp::Impl {
    Package package;NcmContentStorage storage{};nexus::Target source{};u64 generation{};bool ns{};
    ~Impl(){if(serviceIsActive(&storage.s))ncmContentStorageClose(&storage);if(ns)ns::Exit();}
};
VirtualNsp::VirtualNsp()=default;
VirtualNsp::~VirtualNsp()=default;
Result VirtualNsp::Open(u64 app,nexus::Target source,u64 required_generation){
    impl.reset();auto state=std::make_unique<Impl>();R_TRY(ns::Initialize());state->ns=true;state->source=source;
    R_TRY(OpenStorage(source,state->storage,&state->generation));
    if(source==nexus::Target::Usb)R_UNLESS(required_generation && state->generation==required_generation,nexus::Unavailable);
    DumpReport report;const auto rc=BuildPackage(nullptr,app,source,state->storage,state->generation,state->package,report);
    nexus::Log("pc-virtual-nsp-open",rc,app);R_TRY(rc);impl=std::move(state);return 0;
}
u64 VirtualNsp::Size()const{return impl?impl->package.size:0;}
Result VirtualNsp::Read(u64 offset,void* data,u64 size,u64& actual){
    actual=0;R_UNLESS(impl && nexus::lifecycle::CanPoll(),Invalid);
    return ReadPackage(impl->package,impl->source,impl->storage,impl->generation,offset,data,size,actual);
}
std::string DumpReport::Message() const {
    const auto location=std::string(nexus::TargetName(destination))+":";
    if(completed)return "NSP backup saved and verified.\n\n"+location+path+"\n\nInstalled files and saves are unchanged. Keep this backup private."+
        (destination==nexus::Target::Sd?" On FAT32, a large NSP may appear as a split-file folder on a PC.":"")+
        (partial_kept?"\nTemporary cleanup could not be confirmed: "+location+path+".partial":"");
    return "Backup stopped while: "+stage+".\n\n"+(detail.empty()?"Check the connection and free space on "+location+", then try again.":detail)+
        "\n\nInstalled files and saves are unchanged."+(renamed?std::string{}:" No completed NSP was published.")+
        (partial_kept?"\nTemporary data may remain at "+location+path+".partial":"")+
        "\nDetails: switch/nexusmanager/nexus.log";
}

Result DumpGameToNsp(ui::ProgressBox* p,u64 app,const std::string& name,nexus::Target source,nexus::Target destination,DumpReport& report) {
    report={};report.destination=destination;
    nexus::Log("dump-begin-N2",0,app);nexus::Log("dump-source-N2",0,static_cast<u64>(source));
    nexus::Log("dump-destination-N2",0,static_cast<u64>(destination));
    const auto result=[&]() -> Result {
        R_UNLESS(nexus::ValidTarget(source),Invalid);
        R_UNLESS(destination==nexus::Target::Sd || destination==nexus::Target::Usb,Invalid);
        R_TRY(p->ShouldExitResult());
        R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());
        NcmContentStorage cs{};u64 gen{};
        R_TRY(OpenStorage(source,cs,&gen));ON_SCOPE_EXIT(ncmContentStorageClose(&cs));
        Package pkg;R_TRY(BuildPackage(p,app,source,cs,gen,pkg,report));
        report.size=pkg.size;
        report.stage="Checking destination space";
        BackupOutput output;R_TRY(output.Open(destination));
        if(source==nexus::Target::Usb && destination==nexus::Target::Usb)R_UNLESS(gen==output.generation,nexus::Unavailable);
        u64 free{};R_TRY(output.Space(free));
        if(free<Reserve || pkg.size>free-Reserve){
            report.detail="There is not enough free space on "+std::string(nexus::TargetName(destination))+" for this backup (plus 32 MB safety space).";return Invalid;
        }
        R_TRY(p->ShouldExitResult());
        R_TRY(output.Directories());
        char suffix[100]{};
        snprintf(suffix,sizeof(suffix)," [%016llX][%s]-%016llX.nsp",static_cast<unsigned long long>(app),nexus::TargetName(source),static_cast<unsigned long long>(armGetSystemTick()));
        report.path="/dumps/NSP/"+nexus::dump::SafeName(name)+suffix;
        const std::string final_path{report.path},partial{report.path+".partial"};
        // Only exact NotFound allows creation. Never delete a preexisting dump.
        for(const auto& path:{final_path,partial}){
            bool directory{};const auto rc=output.Stat(path,directory);
            if(R_SUCCEEDED(rc)){report.detail="A backup with this name already exists. Try again to create a new name.";return Invalid;}
            R_UNLESS(rc==FsError_PathNotFound,rc);
        }
        report.stage="Creating the backup";
        bool owned=false;
        ON_SCOPE_EXIT({
            if(owned){
                const auto cleanup=output.Cleanup();
                report.partial_kept=R_FAILED(cleanup);
                nexus::Log("dump-partial-cleanup-N2",cleanup);
            }
        });
        const auto create=output.Create(partial,pkg.size,owned);
        if(R_FAILED(create)){
            if(destination==nexus::Target::Usb)report.detail="USB could not create the backup. Check free space and write access. Backups of 4 GiB or more need exFAT; FAT32 USB is limited to smaller files.";
            return create;
        }
        std::vector<u8> buffer(1024*1024);
        std::array<u8,32> written_hash{},read_hash{};
        const auto cancel=[&]{return p->ShouldExitResult();};
        const auto progress=[&](u64 off,u64 size){report.offset=off;p->UpdateTransferBytes(off,size);};
        {
            ui::ProgressScope copying(p,0,5000);
            R_TRY(output.OpenWrite());ON_SCOPE_EXIT(output.Close());
            Sha256Context hash;sha256ContextCreate(&hash);
            report.stage="Writing the NSP backup";p->NewTransfer(report.stage);report.offset=0;
            R_TRY(nexus::dump::Stream(pkg.size,buffer,cancel,
                [&](u64 off,void* dst,u64 size,u64& actual){return ReadPackage(pkg,source,cs,gen,off,dst,size,actual);},
                [&](u64 off,const void* src,u64 size)->Result{
                    R_TRY(output.Write(off,src,size));sha256ContextUpdate(&hash,src,size);return 0;
                },progress,Invalid));
            report.stage="Flushing the backup";R_TRY(output.FlushWrite());
            sha256ContextGetHash(&hash,written_hash.data());
        }
        R_TRY(output.Commit());
        {
            ui::ProgressScope verifying(p,5000,9800);
            R_TRY(output.OpenRead(pkg.size));ON_SCOPE_EXIT(output.Close());
            Sha256Context hash;sha256ContextCreate(&hash);
            report.stage="Verifying the saved backup";p->NewTransfer(report.stage);report.offset=0;
            R_TRY(nexus::dump::Stream(pkg.size,buffer,cancel,
                [&](u64 off,void* dst,u64 count,u64& actual){return output.Read(off,dst,count,actual);},
                [&](u64,const void* src,u64 count)->Result{sha256ContextUpdate(&hash,src,count);return 0;},progress,Invalid));
            sha256ContextGetHash(&hash,read_hash.data());
        }
        if(written_hash!=read_hash){report.detail="The saved backup did not match the bytes read from the game. Check the destination drive before retrying.";return Invalid;}
        R_TRY(cancel());
        if(source==nexus::Target::Usb){nexus::Client c;R_TRY(c.Check(gen));}
        ui::ProgressScope finishing(p,9800,10000);
        report.stage="Finishing the backup";p->NewTransfer(report.stage);
        R_TRY(output.Rename(final_path));report.renamed=true;
        if(destination==nexus::Target::Sd)owned=false;
        const auto commit=output.Commit();
        if(R_FAILED(commit)){
            // Rename succeeded but persistence is uncertain. Do not claim a
            // completed backup or delete a final file after an ambiguous commit.
            report.detail="The backup was verified, but finalization failed. A file may exist at "+std::string(nexus::TargetName(destination))+":"+report.path+". Do not rely on it until checked.";
            return commit;
        }
        report.completed=true;R_SUCCEED();
    }();
    nexus::Log("dump-finished-N2",result,report.offset);
    nexus::Log("dump-size-N2",0,report.size);
    // Stage contains only fixed text; no license, key or ticket bytes in logs.
    nexus::Log(report.stage.c_str(),result);
    return result;
}
}
