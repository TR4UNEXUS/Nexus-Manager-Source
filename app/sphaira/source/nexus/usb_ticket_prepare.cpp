#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_ticket_prepare.hpp"
#include "nexus/usb_ticket_store.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "yati/nx/es.hpp"
#include "defines.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace sphaira::nexusapp {
namespace {
using Digest=std::array<u8,32>;
using Rights=std::array<u8,16>;
constexpr size_t MaxTickets=65536,MaxPart=64*1024;
constexpr Result MissingSd=MAKERESULT(2,1);
Rights Id(const FsRightsId& rights){Rights out{};memcpy(out.data(),&rights,sizeof(rights));return out;}
std::string Code(Result rc){char text[24];snprintf(text,sizeof(text),"0x%08X",rc);return text;}
std::string Identity(const UsbImportTitle& row){char id[24];snprintf(id,sizeof(id),"%016llX",static_cast<unsigned long long>(row.record.key.id));return std::string(id)+" | version "+std::to_string(row.record.key.version);}
const char* Type(u8 type){switch(type){case NcmContentMetaType_Application:return "Base game"_nx;case NcmContentMetaType_Patch:return "Update"_nx;case NcmContentMetaType_AddOnContent:return "DLC";case NcmContentMetaType_DataPatch:return "DLC update"_nx;default:return "Unidentified content"_nx;}}
struct Hash {
    Sha256Context context{};
    Hash(){sha256ContextCreate(&context);}
    void Bytes(const void* data,size_t size){sha256ContextUpdate(&context,data,size);}
    void Number(u64 value){u8 b[8];for(size_t i=0;i<8;++i)b[i]=value>>(i*8);Bytes(b,sizeof(b));}
    void Blob(const void* data,size_t size){Number(size);if(size)Bytes(data,size);}
    Digest Finish(){Digest out{};sha256ContextGetHash(&context,out.data());return out;}
};
Digest PairDigest(const UsbTicketPair& pair){Hash hash;hash.Blob(pair.ticket.data(),pair.ticket.size());hash.Blob(pair.certificate.data(),pair.certificate.size());return hash.Finish();}
Digest ScanDigest(const UsbImportScan& scan){
    std::vector<const UsbImportTitle*> rows;for(const auto& row:scan.titles)rows.push_back(&row);
    std::sort(rows.begin(),rows.end(),[](const auto* a,const auto* b){return std::tie(a->meta_path,a->record.key.id,a->record.key.version,a->record.key.type)<std::tie(b->meta_path,b->record.key.id,b->record.key.version,b->record.key.type);});
    Hash hash;hash.Number(scan.generation);hash.Number(rows.size());
    for(const auto* row:rows){const auto& r=row->record;
        hash.Blob(row->meta_path.data(),row->meta_path.size());hash.Number(r.key.id);hash.Number(r.key.version);hash.Number(r.key.type);hash.Number(r.key.install_type);hash.Number(r.application_id);
        hash.Number(row->status==UsbImportStatus::Incomplete);hash.Blob(r.installed_meta.data(),r.installed_meta.size());hash.Number(r.content.size());
        for(const auto& content:r.content){hash.Number(content.has_expected_hash);hash.Bytes(content.expected_hash.data(),content.expected_hash.size());}
        hash.Number(row->required_tickets.size());for(const auto& rights:row->required_tickets)hash.Bytes(&rights,sizeof(rights));
    }
    return hash.Finish();
}
struct Session {
    ui::ProgressBox* progress;
    u64 generation;
    nexus::Client usb;
    bool es_open{};
    std::set<Rights> common,personal;
    Session(ui::ProgressBox* p,u64 gen):progress(p),generation(gen){}
    ~Session(){if(es_open)es::Exit();}
    Result Check(){R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);R_TRY(progress->ShouldExitResult());R_TRY(nexus::CheckUsbMutationAllowed());return usb.Check(generation);}
    Result Open(){
        R_TRY(Check());R_TRY(es::Initialize());es_open=true;
        for(bool personalised:{false,true}){
            s32 count{},written{};R_TRY(personalised?es::CountPersonalizedTicket(&count):es::CountCommonTicket(&count));
            R_UNLESS(count>=0&&count<=static_cast<s32>(MaxTickets),nexus::Invalid);if(!count)continue;
            std::vector<FsRightsId> ids(count);
            R_TRY(personalised?es::ListPersonalizedTicket(&written,ids.data(),count):es::ListCommonTicket(&written,ids.data(),count));
            R_UNLESS(written==count,nexus::Invalid);auto& dest=personalised?personal:common;
            for(const auto& rights:ids){R_UNLESS(es::IsRightsIdValid(rights),nexus::Invalid);dest.insert(Id(rights));}
        }
        return Check();
    }
};
Result OriginalCommon(const FsRightsId& rights,UsbTicketPair& pair){
    pair={};u64 ts{},cs{};R_TRY(es::GetCommonTicketAndCertificateSize(&ts,&cs,&rights));
    R_UNLESS(ts>=sizeof(es::TicketHmac160)&&ts<=MaxPart&&cs&&cs<=MaxPart,InvalidUsbTicket);
    pair.ticket.resize(ts);pair.certificate.resize(cs);u64 actual_t{},actual_c{};
    R_TRY(es::GetCommonTicketAndCertificateData(&actual_t,&actual_c,pair.ticket.data(),ts,pair.certificate.data(),cs,&rights));
    R_UNLESS(actual_t==ts&&actual_c==cs,InvalidUsbTicket);
    es::TicketData parsed{};R_TRY(es::GetTicketData(pair.ticket,&parsed));
    R_UNLESS(!memcmp(&rights,&parsed.rights_id,sizeof(rights))&&parsed.title_key_type==es::TitleKeyType_Common,InvalidUsbTicket);
    return 0;
}
Result InspectTicket(Session& s,const FsRightsId& rights,UsbTicketPrepareCandidate& out){
    out={};out.rights=rights;R_TRY(s.Check());UsbTicketPair pair;
    const auto archive=LoadUsbTicketPair(s.generation,rights,pair);
    if(R_SUCCEEDED(archive)){out.status=UsbTicketPrepareStatus::AlreadyPrepared;out.digest=PairDigest(pair);return 0;}
    if(archive==InvalidUsbTicket){out.reason="The saved ticket is invalid. It was not overwritten. Reinstall from your original backup."_nx;return 0;}
    R_UNLESS(archive==MissingUsbTicket,archive);
    if(!s.common.contains(Id(rights))){out.reason=s.personal.contains(Id(rights))?
        "This ticket cannot be exported safely by this tool. Reinstall from your original backup on the new SD/system."_nx:
        "This system has no ticket. Try the original SD card and original emuMMC/sysMMC, or reinstall from your original backup."_nx;return 0;}
    const auto extracted=OriginalCommon(rights,pair);
    if(R_FAILED(extracted)){out.reason="The original ticket and certificate could not be read ("_nx+Code(extracted)+"). Reinstall from your original backup."_nx;return s.Check();}
    R_TRY(s.Check());out.status=UsbTicketPrepareStatus::Ready;out.digest=PairDigest(pair);return 0;
}
struct Path {char text[FS_MAX_PATH]{};explicit Path(const std::string& s){snprintf(text,sizeof(text),"%s",s.c_str());}};
Result SaveReport(const UsbTicketPrepareResult& result,std::string& path){
    // Physical SD filesystem, never a USB-routed sdmc view. No ticket bytes,
    // rights IDs, console identifiers or proof digests enter this report.
    FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
    for(const char* dir:{"/switch","/switch/nexusmanager","/switch/nexusmanager/recovery-reports"}){
        Path p(dir);FsDirEntryType type{};const auto rc=fsFsGetEntryType(&sd,p.text,&type);
        if(rc==MissingSd){R_TRY(fsFsCreateDirectory(&sd,p.text));}else{R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir,nexus::Invalid);}
    }
    std::string text="Prepare USB for another SD card\nResult: "_nx+Code(result.result)+"\n"+result.detail+"\n\n";
    for(const auto& entry:result.entries)text+=entry+"\n";
    text+="\nGame files, saves and system registration were not changed. Existing ticket archives were not overwritten.\n"_nx;
    char name[128];snprintf(name,sizeof(name),"/switch/nexusmanager/recovery-reports/prepare-%016llx.txt",static_cast<unsigned long long>(armGetSystemTick()));
    Path temporary(std::string(name)+".tmp"),final(name);R_TRY(fsFsCreateFile(&sd,temporary.text,text.size(),0));
    FsFile file{};R_TRY(fsFsOpenFile(&sd,temporary.text,FsOpenMode_Write,&file));
    auto rc=fsFileWrite(&file,0,text.data(),text.size(),FsWriteOption_Flush);fsFileClose(&file);R_TRY(rc);
    R_TRY(fsFsOpenFile(&sd,temporary.text,FsOpenMode_Read,&file));u64 got{};s64 size{};std::vector<char> check(text.size());
    rc=fsFileGetSize(&file,&size);if(R_SUCCEEDED(rc))rc=fsFileRead(&file,0,check.data(),check.size(),FsReadOption_None,&got);fsFileClose(&file);R_TRY(rc);
    R_UNLESS(size==static_cast<s64>(text.size())&&got==text.size()&&!memcmp(check.data(),text.data(),text.size()),nexus::Invalid);
    R_TRY(fsFsCommit(&sd));R_TRY(fsFsRenameFile(&sd,temporary.text,final.text));R_TRY(fsFsCommit(&sd));path="SD:"+std::string(name);return 0;
}
}

const char* UsbTicketPrepareStatusText(UsbTicketPrepareStatus status){switch(status){case UsbTicketPrepareStatus::Ready:return "Ready to prepare"_nx;case UsbTicketPrepareStatus::AlreadyPrepared:return "Already prepared"_nx;case UsbTicketPrepareStatus::NeedsBackup:return "Needs original backup"_nx;case UsbTicketPrepareStatus::Incomplete:return "Skipped"_nx;}return "Skipped"_nx;}
std::string UsbTicketPreparePreview(const UsbTicketPreparePlan& plan){
    size_t ready{},already{},backup{},skipped{};
    for(const auto& row:plan.titles)switch(row.status){case UsbTicketPrepareStatus::Ready:++ready;break;case UsbTicketPrepareStatus::AlreadyPrepared:++already;break;case UsbTicketPrepareStatus::NeedsBackup:++backup;break;case UsbTicketPrepareStatus::Incomplete:++skipped;break;}
    return std::to_string(ready)+" titles ready to prepare; "_nx+std::to_string(already)+" already prepared; "_nx+std::to_string(backup)+" need the original backup; "_nx+std::to_string(skipped)+" skipped.\n\n"+std::to_string(plan.tickets_to_save)+" small ticket/certificate copies will be saved on USB. Game files and saves will not be copied.\n\nUse the drive with another SD card or emuMMC/sysMMC on this same Switch, then choose Use games from this USB."_nx;
}
Result PlanUsbTicketPreparation(ui::ProgressBox* progress,const UsbImportScan& scan,UsbTicketPreparePlan& plan){
    plan={};R_UNLESS(progress&&scan.complete&&scan.generation&&scan.titles.size()<=4096,nexus::Invalid);Session session(progress,scan.generation);R_TRY(session.Open());
    plan.generation=scan.generation;plan.source_digest=ScanDigest(scan);progress->SetTitle("Prepare USB for another SD card"_nx);progress->NewTransfer("Checking existing tickets; no changes"_nx);
    std::map<std::pair<u64,u8>,u32> newest;for(const auto& row:scan.titles)if(row.record.key.id){auto& version=newest[{row.record.key.id,row.record.key.type}];version=std::max(version,row.record.key.version);}
    std::map<Rights,size_t> known;std::set<size_t> selected;
    for(const auto& row:scan.titles){
        R_TRY(session.Check());UsbTicketPrepareTitle title;title.name=row.name+" - "+Type(row.record.key.type);title.identity=Identity(row);
        if(!row.record.key.id||row.status==UsbImportStatus::Incomplete){title.reason=row.reason.empty()?"The game content could not be checked. Reinstall from your original backup."_nx:row.reason;}
        else if(row.record.key.version!=newest[{row.record.key.id,row.record.key.type}])title.reason="A newer version is on this drive. Only the newest version is prepared."_nx;
        else{
            title.status=UsbTicketPrepareStatus::AlreadyPrepared;
            for(const auto& rights:row.required_tickets){
                R_UNLESS(es::IsRightsIdValid(rights),nexus::Invalid);const auto id=Id(rights);auto it=known.find(id);
                if(it==known.end()){R_UNLESS(plan.tickets.size()<MaxTickets,nexus::Invalid);UsbTicketPrepareCandidate ticket;R_TRY(InspectTicket(session,rights,ticket));it=known.emplace(id,plan.tickets.size()).first;plan.tickets.push_back(std::move(ticket));}
                title.tickets.push_back(it->second);const auto& ticket=plan.tickets[it->second];
                if(ticket.status==UsbTicketPrepareStatus::NeedsBackup){title.status=UsbTicketPrepareStatus::NeedsBackup;if(title.reason.empty())title.reason=ticket.reason;}
                else if(ticket.status==UsbTicketPrepareStatus::Ready&&title.status!=UsbTicketPrepareStatus::NeedsBackup)title.status=UsbTicketPrepareStatus::Ready;
            }
            if(title.status==UsbTicketPrepareStatus::Ready){title.reason="The original ticket/certificate can be saved from this system."_nx;for(auto i:title.tickets)if(plan.tickets[i].status==UsbTicketPrepareStatus::Ready)selected.insert(i);}
            else if(title.status==UsbTicketPrepareStatus::AlreadyPrepared)title.reason=title.tickets.empty()?"No ticket is required by this content."_nx:"All required tickets are already saved on this USB for this Switch."_nx;
        }
        plan.titles.push_back(std::move(title));progress->UpdateTransfer(plan.titles.size(),scan.titles.size());
    }
    R_TRY(session.Check());plan.tickets_to_save=selected.size();plan.complete=true;plan.detail=UsbTicketPreparePreview(plan);return 0;
}
Result PrepareUsbTickets(ui::ProgressBox* progress,const UsbTicketPreparePlan& plan,UsbTicketPrepareResult& out){
    out={};std::set<size_t> selected,saved,confirmed_archives;std::map<size_t,std::string> failures;bool checked_source{};
    const auto operation=[&]() -> Result {
        R_UNLESS(progress&&plan.complete&&plan.generation&&plan.titles.size()<=4096&&plan.tickets.size()<=MaxTickets,nexus::Invalid);
        Session session(progress,plan.generation);R_TRY(session.Open());
        UsbImportScan current;R_TRY(ScanUsbImports(progress,current,plan.generation));R_UNLESS(current.complete&&ScanDigest(current)==plan.source_digest,nexus::Invalid);
        checked_source=true;
        // Current-system tickets make the import scanner skip archive lookup.
        // Therefore explicitly recheck every saved archive relied upon by the
        // preview, including already-prepared titles and mixed old/new pairs.
        std::set<size_t> relied;
        for(const auto& row:plan.titles)if(row.status==UsbTicketPrepareStatus::Ready||row.status==UsbTicketPrepareStatus::AlreadyPrepared)for(auto i:row.tickets){R_UNLESS(i<plan.tickets.size(),nexus::Invalid);if(plan.tickets[i].status==UsbTicketPrepareStatus::Ready)selected.insert(i);else relied.insert(i);}
        for(auto i:relied){
            R_TRY(session.Check());UsbTicketPair pair;const auto rc=LoadUsbTicketPair(plan.generation,plan.tickets[i].rights,pair);
            if(R_FAILED(rc)){failures[i]="A previously saved ticket is missing or could not be checked. Scan again; no replacement was written."_nx;return rc;}
            confirmed_archives.insert(i);
        }
        R_UNLESS(selected.size()==plan.tickets_to_save,nexus::Invalid);
        progress->SetTitle("Prepare USB for another SD card"_nx);progress->NewTransfer("Saving small ticket copies; games stay on USB"_nx);
        size_t done{};
        for(auto index:selected){
            R_TRY(session.Check());const auto& candidate=plan.tickets[index];UsbTicketPair pair;
            const auto existing=LoadUsbTicketPair(plan.generation,candidate.rights,pair);
            if(R_SUCCEEDED(existing)){saved.insert(index);progress->UpdateTransfer(++done,selected.size());continue;}
            if(existing==InvalidUsbTicket){failures[index]="The saved ticket changed or is invalid. Nothing was overwritten; scan again."_nx;return existing;}
            R_UNLESS(existing==MissingUsbTicket,existing);
            const auto extracted=OriginalCommon(candidate.rights,pair);
            if(R_FAILED(extracted)||PairDigest(pair)!=candidate.digest){failures[index]="The original ticket changed or could not be read. Nothing was written for it; scan again or use the original backup."_nx;return R_FAILED(extracted)?extracted:nexus::Invalid;}
            R_TRY(session.Check());const auto stored=SaveUsbTicketPair(plan.generation,candidate.rights,pair.ticket,pair.certificate);
            if(R_FAILED(stored)){failures[index]="The ticket copy could not be confirmed ("_nx+Code(stored)+"). Keep the original backup and scan again."_nx;return stored;}
            saved.insert(index);++out.saved;progress->UpdateTransfer(++done,selected.size());R_TRY(session.Check());
        }
        return session.Check();
    };
    out.result=operation();
    for(const auto& row:plan.titles){
        const auto label=row.name+" ["+row.identity+"]";
        std::string reason=row.reason;auto status=row.status;
        if(status==UsbTicketPrepareStatus::Ready||status==UsbTicketPrepareStatus::AlreadyPrepared){
            bool complete=checked_source;reason=checked_source?reason:"The drive could not be rechecked. No preparation was confirmed; scan again."_nx;
            for(auto i:row.tickets){if(i>=plan.tickets.size()){complete=false;break;}const bool verified=plan.tickets[i].status==UsbTicketPrepareStatus::Ready?saved.contains(i):confirmed_archives.contains(i);if(!verified){complete=false;const auto failure=failures.find(i);reason=failure==failures.end()?"Not finished. Any completed ticket copies were kept; scan again to continue."_nx:failure->second;}}
            if(complete&&status==UsbTicketPrepareStatus::Ready){++out.prepared;out.entries.push_back("Prepared: "_nx+label+" - Tickets are saved for this Switch. Import will check them in the other SD/system."_nx);continue;}
            if(complete){++out.already_prepared;out.entries.push_back("Already prepared: "_nx+label+" - "+row.reason);continue;}
            status=UsbTicketPrepareStatus::Incomplete;
        }
        if(status==UsbTicketPrepareStatus::AlreadyPrepared)++out.already_prepared;else if(status==UsbTicketPrepareStatus::NeedsBackup)++out.needs_backup;else ++out.skipped;
        out.entries.push_back(std::string(UsbTicketPrepareStatusText(status))+": "+label+" - "+reason);
    }
    out.detail=std::string(R_SUCCEEDED(out.result)?"Preparation finished."_nx:"Preparation stopped. Completed ticket copies were kept."_nx)+"\n\nPrepared: "_nx+std::to_string(out.prepared)+"   Already prepared: "_nx+std::to_string(out.already_prepared)+"\nNeeds original backup: "_nx+std::to_string(out.needs_backup)+"   Skipped: "_nx+std::to_string(out.skipped)+"\nNew ticket copies saved: "_nx+std::to_string(out.saved)+"\n\nGame files, saves and system registration were not changed."_nx;
    const auto report=SaveReport(out,out.report_path);if(R_FAILED(report))out.detail+="\nThe SD report could not be saved. Details are still available here."_nx;
    return out.result;
}
}
