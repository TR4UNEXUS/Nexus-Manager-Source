// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/update.hpp"
#include "nexus/update_transaction.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/network_socket_config.hpp"
#include "yati/yati.hpp"
#include "ui/progress_box.hpp"
#include "utils/thread.hpp"
#include "app.hpp"
#include "defines.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <atomic>
#include <chrono>
namespace nexus::updates {
namespace {
using sphaira::ui::ProgressBox;
struct Network {
    bool sockets{},nifm{},curl{};
    Result Open(bool startup=false){
        const auto defaults=*socketGetDefaultInitConfig();
        const auto config=startup?nexus::network::DownloadSocketConfig(defaults):defaults;
        auto rc=socketInitialize(&config);
        if(startup && (rc==MAKERESULT(Module_Libnx,LibnxError_OutOfMemory) ||
            rc==MAKERESULT(Module_Kernel,KernelError_OutOfMemory)))rc=socketInitialize(&defaults);
        // libnx sockets are a singleton, not reference counted. Never borrow
        // or tear down a network session which another feature already owns.
        R_TRY(rc);sockets=true;
        R_TRY(nifmInitialize(NifmServiceType_User));nifm=true;
        NifmInternetConnectionType type{};u32 strength{};NifmInternetConnectionStatus status{};
        R_TRY(nifmGetInternetConnectionStatus(&type,&strength,&status));
        R_UNLESS(status==NifmInternetConnectionStatus_Connected,Unavailable);
        R_UNLESS(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK,Unavailable);curl=true;R_SUCCEED();
    }
    ~Network(){if(curl)curl_global_cleanup();if(nifm)nifmExit();if(sockets)socketExit();}
};
using Clock=std::chrono::steady_clock;
struct Request {
    ProgressBox* progress{};
    const std::atomic_bool* cancel{};
    Clock::time_point deadline{};
    bool Stopped() const {
        return (cancel && cancel->load(std::memory_order_relaxed)) ||
            (progress && progress->ShouldExit()) || Clock::now()>=deadline;
    }
    long RemainingMilliseconds() const {
        return std::max<long>(1,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count());
    }
};
struct Body {std::vector<char> bytes;size_t limit;Request* request;};
size_t Write(char* bytes,size_t size,size_t count,void* ptr){
    auto& body=*static_cast<Body*>(ptr);
    if(size && count>SIZE_MAX/size)return 0;
    const auto n=size*count;
    if(n>body.limit-body.bytes.size() || body.request->Stopped())return 0;
    body.bytes.insert(body.bytes.end(),bytes,bytes+n);return n;
}
int Progress(void* ptr,curl_off_t total,curl_off_t done,curl_off_t,curl_off_t){
    auto* request=static_cast<Request*>(ptr);if(request->Stopped())return 1;
    if(request->progress && total>0)request->progress->UpdateTransferBytes(done,total);
    return 0;
}
bool AllowedRedirect(std::string_view url){
    return url.starts_with("https://release-assets.githubusercontent.com/") ||
        url.starts_with("https://objects.githubusercontent.com/");
}
Result Fetch(ProgressBox* p,std::string url,size_t limit,std::vector<char>& bytes,std::string& detail,
    const std::atomic_bool* cancel=nullptr,long timeout_ms=180000,long connect_ms=15000){
    Request request{p,cancel,Clock::now()+std::chrono::milliseconds(timeout_ms)};
    const auto user_agent=std::string("Nexus-Manager/")+Version;
    R_TRY(romfsInit());ON_SCOPE_EXIT(romfsExit());
    Body body{{},limit,&request};
    for(unsigned redirect=0;redirect<4;++redirect){
        R_UNLESS(!request.Stopped(),Unavailable);
        auto* handle=curl_easy_init();R_UNLESS(handle,Unavailable);
        ON_SCOPE_EXIT(curl_easy_cleanup(handle));
        body.bytes.clear();
        curl_easy_setopt(handle,CURLOPT_URL,url.c_str());
        curl_easy_setopt(handle,CURLOPT_USERAGENT,user_agent.c_str());
        curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,1L);
        curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(handle,CURLOPT_CAINFO,"romfs:/certs/cacert.pem");
        curl_easy_setopt(handle,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
        curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,0L);
        curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT_MS,std::min(connect_ms,request.RemainingMilliseconds()));
        curl_easy_setopt(handle,CURLOPT_TIMEOUT_MS,request.RemainingMilliseconds());
        curl_easy_setopt(handle,CURLOPT_LOW_SPEED_TIME,25L);
        curl_easy_setopt(handle,CURLOPT_LOW_SPEED_LIMIT,128L);
        curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,Write);
        curl_easy_setopt(handle,CURLOPT_WRITEDATA,&body);
        curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,Progress);
        curl_easy_setopt(handle,CURLOPT_XFERINFODATA,&request);
        curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L);
        const auto rc=curl_easy_perform(handle);
        if(rc!=CURLE_OK){detail=std::string("HTTPS request failed: ")+curl_easy_strerror(rc)+". Check Wi-Fi and the console clock.";return Unavailable;}
        long code{};curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&code);
        if(code==200){R_UNLESS(!request.Stopped(),Unavailable);bytes=std::move(body.bytes);R_SUCCEED();}
        if(code==301 || code==302 || code==307 || code==308){
            char* next{};curl_easy_getinfo(handle,CURLINFO_REDIRECT_URL,&next);
            if(next && AllowedRedirect(next)){url=next;continue;}
        }
        detail="GitHub returned HTTP "+std::to_string(code)+". Try again later.";return Unavailable;
    }
    detail="Too many GitHub redirects.";return Invalid;
}
std::string Hex(const unsigned char* bytes,size_t size){
    constexpr char digits[]="0123456789abcdef";std::string text;
    for(size_t i=0;i<size;++i){text+=digits[bytes[i]>>4];text+=digits[bytes[i]&15];}return text;
}
struct StartupCheck {
    Thread thread{};
    std::atomic_bool cancel{},complete{},request_finished{};
    u32 resolver_handle{};
    Clock::time_point began{};
    bool attempted{},thread_started{},consumed{};
    Artifact kind{Artifact::Nro};
    Release release;
} startup_check;

void StartupDeadline(void*){
    // Switch libcurl uses synchronous DNS; CURLOPT_TIMEOUT cannot interrupt
    // getaddrinfo. This handle belongs to the request thread only. Repeating
    // cancellation also covers a DNS call starting just after the first cancel.
    while(!startup_check.request_finished.load(std::memory_order_acquire)){
        const auto elapsed=Clock::now()-startup_check.began;
        if(elapsed>=std::chrono::seconds(8))startup_check.cancel.store(true,std::memory_order_relaxed);
        if(startup_check.cancel.load(std::memory_order_relaxed) || elapsed>=std::chrono::seconds(3))
            resolverCancel(startup_check.resolver_handle);
        svcSleepThread(50*1000*1000);
    }
}
void StartupWorker(void*){
    startup_check.resolver_handle=resolverGetCancelHandle();
    Thread deadline_thread{};
    startup_check.began=Clock::now();
    if(!startup_check.resolver_handle ||
        R_FAILED(sphaira::utils::CreateThread(&deadline_thread,StartupDeadline,nullptr,32*1024))){
        startup_check.complete.store(true,std::memory_order_release);return;
    }
    if(R_FAILED(threadStart(&deadline_thread))){
        threadClose(&deadline_thread);startup_check.complete.store(true,std::memory_order_release);return;
    }
    // Destroy sockets, curl and romfs before publishing completion. Main-thread
    // network entry points join this worker before opening their own sessions.
    {
        Network network;
        if(!startup_check.cancel.load(std::memory_order_relaxed) && R_SUCCEEDED(network.Open(true))){
            std::vector<char> bytes;std::string detail;Release release;
            if(R_SUCCEEDED(Fetch(nullptr,ReleaseApi,2*1024*1024,bytes,detail,&startup_check.cancel,8000,3000)) &&
                SelectRelease({bytes.data(),bytes.size()},release,startup_check.kind) && ValidRelease(release) &&
                VersionCode(release.version)>VersionCode(Version) && !startup_check.cancel.load(std::memory_order_relaxed))
                startup_check.release=std::move(release);
        }
    }
    startup_check.request_finished.store(true,std::memory_order_release);
    threadWaitForExit(&deadline_thread);
    threadClose(&deadline_thread);
    startup_check.complete.store(true,std::memory_order_release);
}
void JoinStartupWorker(){
    if(!startup_check.thread_started)return;
    threadWaitForExit(&startup_check.thread);
    threadClose(&startup_check.thread);
    startup_check.thread_started=false;
}
}
void StartStartupCheck(Artifact kind){
    if(startup_check.attempted)return;
    startup_check.attempted=true;
    if(kind!=Artifact::Nro && kind!=Artifact::Nsp)return;
    startup_check.kind=kind;
    if(R_FAILED(sphaira::utils::CreateThread(&startup_check.thread,StartupWorker,nullptr,128*1024)))return;
    if(R_FAILED(threadStart(&startup_check.thread))){threadClose(&startup_check.thread);return;}
    startup_check.thread_started=true;
}
bool TakeStartupRelease(Release& release){
    if(startup_check.consumed || !startup_check.complete.load(std::memory_order_acquire))return false;
    // Completion is published only after all blocking work and resource cleanup.
    JoinStartupWorker();
    startup_check.consumed=true;
    if(!ValidRelease(startup_check.release))return false;
    release=std::move(startup_check.release);
    return true;
}
void StopStartupCheck(){
    startup_check.cancel.store(true,std::memory_order_relaxed);
    JoinStartupWorker();
}
Result Check(ProgressBox* p,Release& release,std::string& detail){
    detail="Connect the Switch to the internet, then try again.";
    Network network;R_TRY(network.Open());
    std::vector<char> bytes;R_TRY(Fetch(p,ReleaseApi,2*1024*1024,bytes,detail));
    if(!SelectRelease({bytes.data(),bytes.size()},release,envIsNso()?Artifact::Nsp:Artifact::Nro)){
        detail="No verified Nexus Manager update is published for this edition yet.";return Unavailable;
    }
    R_SUCCEED();
}
Result Install(ProgressBox* p,const Release& release,std::string& detail){
    const bool standalone=envIsNso();
    detail="Update refused: incorrect app, release or launch mode. Use Nexus Manager in full-memory mode.";
    R_UNLESS(!sphaira::App::IsApplet() && ValidRelease(release) &&
        release.kind==(standalone?Artifact::Nsp:Artifact::Nro) && VersionCode(release.version)>VersionCode(Version),Invalid);
    if(!standalone)R_UNLESS(!strcasecmp(sphaira::App::GetExePath(),"/switch/nexusmanager/NexusManager.nro"),Invalid);
    NcmStorageId install_storage=NcmStorageId_None;
    if(standalone){
        detail="Could not identify this installed Manager app. The existing app was kept.";
        u64 program_id{};NcmStorageId update_storage=NcmStorageId_None;
        R_UNLESS(appletGetAppletType()==AppletType_Application,Invalid);
        R_TRY(svcGetInfo(&program_id,InfoType_ProgramId,CUR_PROCESS_HANDLE,0));
        R_UNLESS(program_id==0x05004e584d470000ULL,Invalid);
        R_TRY(appletGetLaunchStorageInfoForDebug(&install_storage,&update_storage));
        R_UNLESS(install_storage==NcmStorageId_SdCard || install_storage==NcmStorageId_BuiltInUser,Invalid);
        R_TRY(sphaira::yati::CheckManagerUpdateStorage(install_storage,detail));
    }
    struct Store {
        bool Regular(const char* path){struct stat st{};return stat(path,&st)==0 && S_ISREG(st.st_mode);}
        bool Absent(const char* path){struct stat st{};return stat(path,&st)!=0 && errno==ENOENT;}
        bool Rename(const char* a,const char* b){return rename(a,b)==0;}
        bool Commit(){auto* fs=fsdevGetDeviceFileSystem("sdmc:");return fs && R_SUCCEEDED(fsFsCommit(fs));}
    } store;
    constexpr const char* nro="sdmc:/switch/nexusmanager/NexusManager.nro";
    if(!standalone)R_UNLESS(store.Regular(nro),Invalid);
    detail="Connect the Switch to the internet, then try again. The installed app was kept.";
    std::vector<char> bytes;
    {Network network;R_TRY(network.Open());R_TRY(Fetch(p,release.url,release.size,bytes,detail));}
    unsigned char digest[32];sha256CalculateHash(digest,bytes.data(),bytes.size());
    detail="Downloaded update failed validation. The installed app was kept.";
    R_UNLESS(bytes.size()==release.size && Hex(digest,32)==release.sha256 &&
        (standalone?ValidNsp({bytes.data(),bytes.size()}):ValidNro({bytes.data(),bytes.size()},release.version)),Invalid);
    const auto token=std::to_string(armGetSystemTick());
    const char* folder="sdmc:/switch/nexusmanager/updates";
    detail="Could not create the Manager update folder on SD. The installed app was kept.";
    if(mkdir(folder,0777)!=0){struct stat st{};R_UNLESS(errno==EEXIST && stat(folder,&st)==0 && S_ISDIR(st.st_mode),Unavailable);}
    const auto temp=std::string(folder)+"/download-"+token+".part";
    const auto backup=std::string(folder)+"/NexusManager-"+Version+"-"+token+".nro.bak";
    const auto current=standalone?std::string(folder)+"/NexusManager-"+release.version+"-"+token+".nsp":std::string(nro);
    detail="Could not stage the update on SD. Current app was kept.";
    FILE* f=fopen(temp.c_str(),"wbx");R_UNLESS(f,Unavailable);
    bool partial=true;ON_SCOPE_EXIT(if(partial)remove(temp.c_str()));
    const auto written=fwrite(bytes.data(),1,bytes.size(),f);
    const auto flushed=fflush(f);const auto synced=fsync(fileno(f));const auto closed=fclose(f);
    R_UNLESS(written==bytes.size() && !flushed && !synced && !closed,Unavailable);
    // Re-read the staged bytes before changing the executable path.
    f=fopen(temp.c_str(),"rb");R_UNLESS(f,Unavailable);
    Sha256Context hash;sha256ContextCreate(&hash);char chunk[65536];size_t got,total=0;
    while((got=fread(chunk,1,sizeof(chunk),f))){sha256ContextUpdate(&hash,chunk,got);total+=got;}
    const auto failed=ferror(f);fclose(f);sha256ContextGetHash(&hash,digest);
    R_UNLESS(!failed && total==release.size && Hex(digest,32)==release.sha256,Invalid);
    R_UNLESS(store.Commit(),Unavailable);
    R_TRY(p->ShouldExitResult());
    const auto result=PublishVerified(store,temp.c_str(),current.c_str(),backup.c_str(),!standalone);
    if(result!=Publish::Ok){
        detail=result==Publish::Restored?"Update stopped; the original app was restored.":
            result==Publish::RestoreFailed || result==Publish::CommitFailed?
                "SD could not confirm the update. Do not retry or remove files. Reconnect SD to a PC and inspect switch/nexusmanager/updates; any .nro.bak is the original app.":
                "Could not publish the update safely. The installed app was not replaced.";
        return Unavailable;
    }
    partial=false;
    std::vector<char>().swap(bytes);
    if(standalone){
        // Install only this verified Manager package into its current native
        // storage. Never inherit the user's game-install destination or skips.
        p->NewTransfer("Installing Nexus Manager");
        detail="The update was downloaded, but installation could not start. Your existing Manager app was kept.";
        fs::FsNativeSd sd;
        R_TRY(sd.GetFsOpenResult());
        R_TRY(sphaira::yati::InstallManagerUpdateFromLocalNsp(p,&sd,current.substr(5),release.version,release.sha256,release.size,install_storage,detail));
    }
    detail="Update installed. Restarting Nexus Manager.";
    R_SUCCEED();
}
}
