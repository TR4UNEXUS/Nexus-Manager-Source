// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/problem_reports.hpp"
#include "nexus/problem_report_core.hpp"
#include "nexus/report_observations.hpp"
#include "nexus/hats_identity.hpp"
#include "nexus/diagnostics.hpp"
#include "nexus/report_config.hpp"
#include "nexus/support_storage.hpp"
#include "nexus/routing_backup.hpp"
#include "nexus/update.hpp"
#include "nexus/localize.hpp"
#include "nexus/lifecycle.hpp"
#include "utils/thread.hpp"
#include "app.hpp"
#include <yyjson.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <ctime>
namespace nexus::reports {
namespace {
constexpr const char* Root="/switch/nexusmanager/problem-reports";
constexpr const char* Choice="/switch/nexusmanager/problem-reports/choice.txt";
constexpr const char* Receipt="/switch/nexusmanager/problem-reports/last-receipt.txt";
std::mutex mutex;std::atomic_int preference{-2};std::set<std::string> seen;
Thread thread{};bool started{},offered{};std::atomic_bool cancel{},finished{};
std::atomic<Result> upload_result{};std::string last_receipt;std::chrono::steady_clock::time_point next_attempt{};
std::atomic_bool resume_pending{};unsigned resume_reports{};bool crash_scanned{};
u32 previous_battery{};bool previous_battery_valid{};std::time_t previous_battery_time{};
std::string Digest(std::string_view bytes){std::array<u8,32> hash{};sha256CalculateHash(hash.data(),bytes.data(),bytes.size());std::string value;constexpr char hex[]="0123456789abcdef";for(auto b:hash){value+=hex[b>>4];value+=hex[b&15];}return value;}
std::string ReadText(support::Storage& s,const char* name,size_t max){support::Bytes b;if(R_FAILED(s.Read(name,max,b)))return {};return {b.begin(),b.end()};}
void ObserveEnvironment(){
    Log("report-os-version",0,hosversionGet());Log("report-runtime-nsp",0,envIsNso());
    Log("report-operation-mode",0,appletGetOperationMode());
    u64 memory{};const auto mem=svcGetInfo(&memory,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);Log("report-memory-used",mem,memory);
    const auto now=std::time(nullptr);const auto opened=psmInitialize();
    if(R_SUCCEEDED(opened)){
        u32 battery{};const auto rc=psmGetBatteryChargePercentage(&battery);Log("report-battery-percent",rc,battery);
        if(previous_battery_valid&&now>=previous_battery_time){Log("report-battery-previous",0,previous_battery);Log("report-battery-sample-gap-seconds",0,now-previous_battery_time);}
        PsmChargerType charger{};const auto charged=psmGetChargerType(&charger);Log("report-charger",charged,charger);
        psmExit();if(R_SUCCEEDED(rc)&&battery<=100){previous_battery=battery;previous_battery_time=now;previous_battery_valid=true;}
    }else Log("report-battery-query",opened);
    u32 strength{};NifmInternetConnectionType type{};NifmInternetConnectionStatus status{};
    if(R_SUCCEEDED(nifmInitialize(NifmServiceType_User))){const auto rc=nifmGetInternetConnectionStatus(&type,&strength,&status);Log("report-network-type",rc,type);Log("report-wifi-strength",rc,strength);Log("report-network-state",rc,status);nifmExit();}
    FsFileSystem sd{};if(R_SUCCEEDED(fsOpenSdCardFileSystem(&sd))){s64 free{},total{};const auto fr=fsFsGetFreeSpace(&sd,"/",&free);const auto tr=fsFsGetTotalSpace(&sd,"/",&total);Log("report-sd-free-bytes",fr,free<0?0:free);Log("report-sd-total-bytes",tr,total<0?0:total);fsFsClose(&sd);}
}
void ObserveModules(support::Storage& s){
    hats::Identity identity;
    if(hats::ReadIdentity(ReadText(s,hats::IdentityPath,4096),identity)){
        Log("report-hats-sequence",0,identity.sequence);Log("report-hats-edition",0,hats::EditionCode(identity.edition));
    }
    std::vector<std::string> names;if(R_FAILED(s.List("/atmosphere/contents",names)))return;
    unsigned count{};for(const auto& name:names){if(!Hex(name,16))continue;support::Bytes flag;
        if(R_SUCCEEDED(s.Read("/atmosphere/contents/"+name+"/flags/boot2.flag",64,flag))){++count;if(count<=12){u64 id{};if(ParseNumber(name,id,16))Log("report-configured-module-id",0,id);}}}
    Log("report-configured-module-count",0,count);
    support::Bytes hoc;const auto rc=s.Read("/atmosphere/kips/hoc.kip",2*1024*1024,hoc);Log("report-hoc-file-query",rc,R_SUCCEEDED(rc));
    // These are configured files, not proof that a module is running.
}
Result Capture(const char* kind,Result result,const std::string& description,std::string& preview,bool automatic=false){
    R_UNLESS(Configured()&&Kind(kind),Unavailable);R_UNLESS(Description(description),Invalid);support::Storage s;R_TRY(s.Open());R_TRY(support::Folders(s,"problem-reports"));
    std::vector<std::string> names;R_TRY(s.List(Root,names));R_UNLESS(size_t(std::count_if(names.begin(),names.end(),[](const auto& n){return QueueName(n);}))<MaxQueued,Unavailable);
    std::string firmware;
    {Client diagnostics;const auto rc=diagnostics.ReportSnapshot(firmware);Log("report-live-snapshot",rc,armTicksToNs(armGetSystemTick()));
     if(R_FAILED(rc)){firmware=ReadText(s,"/atmosphere/logs/usb_storage_status.txt",128*1024);firmware+="\nusb_live_snapshot_current=0\n";}}
    routing_backup::Diagnostic details{};static_cast<void>(sphaira::nexusapp::QueryConsoleRoutingBackupDiagnostic(details));
    ObserveEnvironment();ObserveModules(s);Log("report-game-context",0,diagnostics::CurrentGame());Log("report-origin",0,automatic?1:0);
    const auto manager=ReadText(s,"/switch/nexusmanager/nexus.log",1100*1024);
    Client client;nexus::Status state{};const auto usb=client.Query(state);routing_backup::Status backup{};const auto saved=sphaira::nexusapp::QueryConsoleRoutingBackup(backup);
    char status[256]{};snprintf(status,sizeof(status),"usb_query=0x%08X mounted=%u ready=%u state=%u usb_result=0x%08X backup_query=0x%08X backup_flags=%u backup_result=0x%08X",usb,state.mounted,state.ready,state.state,state.result,saved,backup.flags,backup.last_result);
    const auto payload=Payload(updates::Version,kind,result,description,manager,firmware,status);R_UNLESS(!payload.empty()&&payload.size()<=MaxReport,Invalid);
    const auto id=Digest(payload);support::Bytes existing;const auto path=std::string(Root)+"/"+id+".json";
    const auto found=s.Read(path,MaxReport,existing);if(found!=support::Missing){R_TRY(found);R_UNLESS(std::string(existing.begin(),existing.end())==payload,Invalid);}
    else R_TRY(s.Write(path,{payload.begin(),payload.end()}));
    preview="Report queued. It will upload when Manager is idle and the internet is available."_nx;
    preview+="\n\n"+std::string("Report ID: "_nx)+id+"\n"+"Includes versions, game IDs, error codes, transfer stages, USB and backup status, battery samples, connection type and configured module IDs. Recent crash summaries contain only the program ID, error, type and time. Keys, tickets, passwords, addresses, game files, saves and raw memory are excluded."_nx;
    return 0;
}
bool QueuedPayload(const std::string& payload,bool& manual){
    manual=false;if(payload.empty()||payload.size()>MaxReport)return false;
    auto* doc=yyjson_read(payload.data(),payload.size(),0);if(!doc)return false;ON_SCOPE_EXIT(yyjson_doc_free(doc));
    auto* root=yyjson_doc_get_root(doc);auto* kind=yyjson_obj_get(root,"kind");auto* version=yyjson_obj_get(root,"manager");auto* description=yyjson_obj_get(root,"description");auto* logs=yyjson_obj_get(root,"logs");
    if(!yyjson_is_obj(root)||yyjson_obj_size(root)!=6||!yyjson_is_uint(yyjson_obj_get(root,"schema"))||yyjson_get_uint(yyjson_obj_get(root,"schema"))!=1||!yyjson_is_str(kind)||!Kind({yyjson_get_str(kind),yyjson_get_len(kind)})||
       !yyjson_is_str(version)||!updates::VersionCode(yyjson_get_str(version))||!yyjson_is_uint(yyjson_obj_get(root,"result"))||yyjson_get_uint(yyjson_obj_get(root,"result"))>UINT32_MAX||
       !yyjson_is_str(description)||!Description({yyjson_get_str(description),yyjson_get_len(description)})||!yyjson_is_obj(logs)||yyjson_obj_size(logs)!=3)return false;
    auto* manager=yyjson_obj_get(logs,"manager");auto* firmware=yyjson_obj_get(logs,"firmware");auto* status=yyjson_obj_get(logs,"status");
    if(!yyjson_is_str(manager)||!yyjson_is_str(firmware)||!yyjson_is_str(status)||!StatusFields({yyjson_get_str(status),yyjson_get_len(status)}))return false;
    const std::string m(yyjson_get_str(manager),yyjson_get_len(manager)),f(yyjson_get_str(firmware),yyjson_get_len(firmware));
    if(ManagerLines(m)!=m||FirmwareLines(f)!=f)return false;
    const bool generic=std::string_view(yyjson_get_str(kind),yyjson_get_len(kind))=="manual";
    manual=generic&&!IsAutomaticCapture(m);return generic||yyjson_get_len(description)==0;
}
Result UploadOne(){
    support::Storage s;R_TRY(s.Open());std::vector<std::string> names;R_TRY(s.List(Root,names));std::sort(names.begin(),names.end());
    for(const auto& name:names){
        if(!QueueName(name))continue;if(cancel.load()||!lifecycle::CanPoll())return Unavailable;
        support::Bytes bytes;const auto path=std::string(Root)+"/"+name;R_TRY(s.Read(path,MaxReport,bytes));const std::string payload(bytes.begin(),bytes.end());bool manual{};
        if(!QueuedPayload(payload,manual)||Digest(payload)!=name.substr(0,64))continue;
        if(preference.load()!=1&&!manual)continue;
        std::string receipt;R_TRY(updates::SendDiagnosticReport(payload,receipt,cancel));
        auto* doc=yyjson_read(receipt.data(),receipt.size(),0);R_UNLESS(doc,Invalid);ON_SCOPE_EXIT(yyjson_doc_free(doc));
        auto* root=yyjson_doc_get_root(doc);auto* id=yyjson_obj_get(root,"id");auto* state=yyjson_obj_get(root,"status");
        R_UNLESS(yyjson_is_str(id)&&std::string_view(yyjson_get_str(id),yyjson_get_len(id))==name.substr(0,64)&&yyjson_is_str(state)&&std::string_view(yyjson_get_str(state),yyjson_get_len(state))=="stored",Invalid);
        const auto record=name.substr(0,64)+"\n";R_TRY(s.Write(Receipt,{record.begin(),record.end()}));R_TRY(s.Remove(path));
        std::scoped_lock lock(mutex);last_receipt=name.substr(0,64);return 0;
    }return 0;
}
void CollectIdleObservations(){
    if(preference.load()!=1||cancel.load()||!lifecycle::CanPoll())return;
    std::scoped_lock lock(mutex);
    if(resume_pending.exchange(false)&&resume_reports<4){
        Log("report-auto-applet-resume",0,++resume_reports);std::string preview;
        Log("report-auto-resume-queued",Capture("manual",0,"",preview,true));
    }else if(!previous_battery_valid){ObserveEnvironment();}
    if(crash_scanned||cancel.load()||!lifecycle::CanPoll())return;crash_scanned=true;
    support::Storage s;if(R_FAILED(s.Open()))return;
    std::vector<std::pair<std::string,std::string>> candidates;
    for(const char* folder:{"/atmosphere/crash_reports","/atmosphere/fatal_reports"}){
        std::vector<std::string> names;if(R_FAILED(s.List(folder,names)))continue;
        for(const auto& name:names)if(name.size()==32&&name[11]=='_'&&name.ends_with(".log")&&Hex(std::string_view(name).substr(12,16),16))candidates.emplace_back(name,folder);
    }
    std::sort(candidates.rbegin(),candidates.rend());unsigned considered{};
    constexpr const char* remembered="/switch/nexusmanager/problem-reports/last-crash.txt";
    const auto seen_crash=ReadText(s,remembered,80);const auto now=std::time(nullptr);
    for(const auto& [name,folder]:candidates){
        if(++considered>8)break;if(Digest(name)==seen_crash)break;
        const auto text=ReadText(s,(folder+"/"+name).c_str(),512*1024);const auto info=Crash(name,text);
        if(!info.valid||now<0||info.timestamp>u64(now)+300||u64(now)>info.timestamp+7*24*60*60)continue;
        Log("report-auto-game-crash",info.result,info.program);Log("report-crash-exception-type",0,info.type);Log("report-crash-timestamp",0,info.timestamp);
        Log("report-crash-age-seconds",0,u64(now)>=info.timestamp?u64(now)-info.timestamp:0);
        std::string preview;const auto captured=Capture("manual",info.result,"",preview,true);
        if(R_SUCCEEDED(captured)){const auto id=Digest(name);s.Write(remembered,{id.begin(),id.end()});}break;
    }
}
void Worker(void*){CollectIdleObservations();upload_result=UploadOne();finished.store(true,std::memory_order_release);}
void Join(){if(started){const auto wait=threadWaitForExit(&thread);if(R_FAILED(wait))fatalThrow(wait);const auto close=threadClose(&thread);if(R_FAILED(close))fatalThrow(close);started=false;}}
}
bool Configured(){return std::string_view(Endpoint).starts_with("https://");}
int Preference(){
    if(preference.load()!=-2)return preference.load();std::scoped_lock lock(mutex);if(preference.load()!=-2)return preference.load();
    support::Storage s;if(R_FAILED(s.Open()))return -1;const auto text=ReadText(s,Choice,64);
    preference=text=="NEXUS-REPORTS-2 enabled\n"?1:(text=="NEXUS-REPORTS-2 disabled\n"||text=="NEXUS-REPORTS-1 disabled\n")?0:-1;return preference.load();
}
Result SetPreference(bool enabled){
    Stop();std::scoped_lock lock(mutex);preference=0;support::Storage s;R_TRY(s.Open());R_TRY(support::Folders(s,"problem-reports"));
    if(!enabled)R_TRY(s.Remove(Choice));const std::string value=enabled?"NEXUS-REPORTS-2 enabled\n":"NEXUS-REPORTS-2 disabled\n";
    R_TRY(s.Write(Choice,{value.begin(),value.end()}));preference=enabled?1:0;next_attempt={};return 0;
}
void QueueFailure(const char* kind,Result result){
    if(!result||!Kind(kind))return;
    // Record this request's result before reading the log. Previously a failed
    // install could upload only unrelated errors from an earlier operation.
    const auto event=std::string("report-failure-")+kind;Log(event.c_str(),result);
    if(!Configured()||Preference()!=1)return;
    std::scoped_lock lock(mutex);const auto key=std::string(kind)+"-"+std::to_string(result)+"-"+std::to_string(diagnostics::CurrentGame());if(seen.size()>=16||seen.contains(key))return;
    std::string preview;const auto rc=Capture(kind,result,"",preview,true);if(R_SUCCEEDED(rc))seen.insert(key);Log("RC1-report-queued",rc,result);
}
Result QueueManual(const std::string& description,std::string& preview){std::scoped_lock lock(mutex);return Capture("manual",0,description,preview);}
Result ClearQueued(){Stop();std::scoped_lock lock(mutex);support::Storage s;R_TRY(s.Open());std::vector<std::string> names;R_TRY(s.List(Root,names));for(const auto& name:names)if(QueueName(name))R_TRY(s.Remove(std::string(Root)+"/"+name));return 0;}
std::string Status(){
    const auto pref=Preference();std::scoped_lock lock(mutex);std::string text=pref==1?"Automatic reporting: on"_nx:"Automatic reporting: off"_nx;
    support::Storage s;if(R_SUCCEEDED(s.Open())){std::vector<std::string> names;if(R_SUCCEEDED(s.List(Root,names)))text+="\n"+std::string("Reports waiting: "_nx)+std::to_string(std::count_if(names.begin(),names.end(),[](const auto& n){return QueueName(n);}));
        const auto receipt=ReadText(s,Receipt,80);if(receipt.size()==65&&Hex(std::string_view(receipt).substr(0,64),64))text+="\n\n"+std::string("Last uploaded report: "_nx)+receipt;}
    return text;
}
void Poll(bool idle){
    if(!idle&&started)cancel=true;
    if(started&&finished.load(std::memory_order_acquire)){Join();if(!last_receipt.empty()){sphaira::App::Notify("Diagnostic report uploaded"_nx);last_receipt.clear();}next_attempt=std::chrono::steady_clock::now()+std::chrono::seconds(R_FAILED(upload_result.load())?120:30);}
    if(!idle||started||!Configured()||!lifecycle::CanPoll()||std::chrono::steady_clock::now()<next_attempt)return;
    Preference();cancel=false;finished=false;auto rc=sphaira::utils::CreateThread(&thread,Worker,nullptr,128*1024);
    if(R_SUCCEEDED(rc)){rc=threadStart(&thread);if(R_FAILED(rc))threadClose(&thread);else started=true;}
    if(R_FAILED(rc))next_attempt=std::chrono::steady_clock::now()+std::chrono::minutes(2);
}
void Stop(){cancel=true;Join();}
void OnResume(){resume_pending.store(true);}
bool OfferChoice(){if(offered||!Configured())return false;offered=true;return Preference()==-1;}
}
