// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/release.hpp"
#include "nexus/network_socket_config.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
using Result=unsigned;using u32=unsigned;using curl_off_t=long long;
#define R_TRY(expr) do{const auto fixture_result=(expr);if(fixture_result)return fixture_result;}while(false)
#define R_UNLESS(expr,rc) do{if(!(expr))return (rc);}while(false)
#define R_SUCCEED() return 0
#define R_SUCCEEDED(rc) (!(rc))
#define R_FAILED(rc) (!!(rc))
#define MAKERESULT(module,error) ((module)|((error)<<9))
constexpr unsigned Module_Libnx=345,Module_Kernel=1,LibnxError_OutOfMemory=12,KernelError_OutOfMemory=104;
template<class F>struct Scope{F f;~Scope(){f();}};
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__LINE__){[&]{__VA_ARGS__;}}
namespace nexus {constexpr Result Unavailable=55,Invalid=56;}
namespace mock {
std::string scenario;
std::atomic_bool sockets{},network{},curl{},romfs{},performing{},dns_cancelled{};
std::atomic_uint socket_opens{},socket_closes{},curl_closes{},thread_creates{},thread_starts{},thread_closes{},requests{},dns_cancels{};
std::vector<long> timeouts;
}
namespace sphaira::ui {
struct ProgressBox {bool ShouldExit(){assert(false);return false;}void UpdateTransfer(curl_off_t,curl_off_t){assert(false);}};
}
struct Thread {void(*function)(void*){};void* arg{};std::thread worker;bool created{};};
namespace sphaira::utils {
Result CreateThread(Thread* thread,void(*function)(void*),void* arg,size_t){
    const auto call=++mock::thread_creates;
    if((mock::scenario=="thread-create"&&call==1)||(mock::scenario=="deadline-create"&&call==2))return 1;
    thread->function=function;thread->arg=arg;thread->created=true;return 0;
}
}
Result threadStart(Thread* thread){
    assert(thread->created);const auto call=++mock::thread_starts;
    if((mock::scenario=="thread-start"&&call==1)||(mock::scenario=="deadline-start"&&call==2))return 1;
    thread->worker=std::thread(thread->function,thread->arg);return 0;
}
void threadWaitForExit(Thread* thread){assert(thread->worker.joinable());thread->worker.join();}
void threadClose(Thread* thread){assert(thread->created&&!thread->worker.joinable());thread->created=false;++mock::thread_closes;}
void svcSleepThread(long long ns){std::this_thread::sleep_for(std::chrono::nanoseconds(ns));}
u32 resolverGetCancelHandle(){return 123;}
Result resolverCancel(u32 handle){assert(handle==123);mock::dns_cancelled=true;++mock::dns_cancels;return 0;}
struct SocketInitConfig {unsigned tcp_rx_buf_size{},tcp_rx_buf_max_size{};};
const SocketInitConfig* socketGetDefaultInitConfig(){static SocketInitConfig config;return &config;}
Result socketInitialize(const SocketInitConfig* config){
    ++mock::socket_opens;assert(config->tcp_rx_buf_size==512*1024&&config->tcp_rx_buf_max_size==1024*1024);
    if(mock::sockets)return 9;mock::sockets=true;return 0;
}
void socketExit(){assert(mock::sockets&&!mock::curl&&!mock::network);mock::sockets=false;++mock::socket_closes;}
enum {NifmServiceType_User};using NifmInternetConnectionType=int;using NifmInternetConnectionStatus=int;
constexpr auto NifmInternetConnectionStatus_Connected=1;
Result nifmInitialize(int){assert(mock::sockets);mock::network=true;return 0;}
void nifmExit(){assert(mock::network);mock::network=false;}
Result nifmGetInternetConnectionStatus(int*,u32*,int* status){*status=mock::scenario=="offline"?0:1;return 0;}
Result romfsInit(){mock::romfs=true;return 0;}
void romfsExit(){assert(mock::romfs);mock::romfs=false;}
enum CURLcode{CURLE_OK,CURLE_FAILED};
enum {CURL_GLOBAL_DEFAULT,CURLPROTO_HTTPS=2};
enum CURLoption {CURLOPT_URL,CURLOPT_USERAGENT,CURLOPT_SSL_VERIFYPEER,CURLOPT_SSL_VERIFYHOST,CURLOPT_CAINFO,
    CURLOPT_PROTOCOLS,CURLOPT_FOLLOWLOCATION,CURLOPT_CONNECTTIMEOUT_MS,CURLOPT_TIMEOUT_MS,
    CURLOPT_LOW_SPEED_TIME,CURLOPT_LOW_SPEED_LIMIT,CURLOPT_NOSIGNAL,CURLOPT_WRITEFUNCTION,CURLOPT_WRITEDATA,
    CURLOPT_XFERINFOFUNCTION,CURLOPT_XFERINFODATA,CURLOPT_NOPROGRESS};
enum {CURLINFO_RESPONSE_CODE,CURLINFO_REDIRECT_URL};
using WriteFn=size_t(*)(char*,size_t,size_t,void*);
using ProgressFn=int(*)(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t);
struct CURL {std::string url,ca;long peer{},host{},protocol{},follow{},connect{},timeout{},signal{},http{200};
    WriteFn write{};ProgressFn progress{};void* body{};void* request{};};
CURLcode curl_global_init(int){assert(mock::sockets&&!mock::curl);mock::curl=true;return CURLE_OK;}
void curl_global_cleanup(){assert(mock::curl&&!mock::performing);mock::curl=false;++mock::curl_closes;}
CURL* curl_easy_init(){assert(mock::curl);return new CURL;}
void curl_easy_cleanup(CURL* handle){delete handle;}
template<class T>CURLcode curl_easy_setopt(CURL* h,CURLoption option,T value){
    if constexpr(std::is_integral_v<T>||std::is_enum_v<T>){
        switch(option){case CURLOPT_SSL_VERIFYPEER:h->peer=value;break;case CURLOPT_SSL_VERIFYHOST:h->host=value;break;
        case CURLOPT_PROTOCOLS:h->protocol=value;break;case CURLOPT_FOLLOWLOCATION:h->follow=value;break;
        case CURLOPT_CONNECTTIMEOUT_MS:h->connect=value;break;case CURLOPT_TIMEOUT_MS:h->timeout=value;break;
        case CURLOPT_NOSIGNAL:h->signal=value;break;default:break;}
    }else if constexpr(std::is_same_v<T,WriteFn>)h->write=value;
    else if constexpr(std::is_same_v<T,ProgressFn>)h->progress=value;
    else if constexpr(std::is_same_v<T,const char*>){if(option==CURLOPT_URL)h->url=value;if(option==CURLOPT_CAINFO)h->ca=value;}
    else {if(option==CURLOPT_WRITEDATA)h->body=value;if(option==CURLOPT_XFERINFODATA)h->request=value;}
    return CURLE_OK;
}
std::string Feed(){
    const auto version=mock::scenario=="current"?std::string(nexus::updates::Version):"9.0.0";
    const auto tag="v"+version;
    const auto asset=mock::scenario=="new-nsp"?"NexusManager.nsp":"NexusManager.nro";
    return "[{\"tag_name\":\""+tag+"\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\""+asset+
        "\",\"state\":\"uploaded\",\"size\":2000000,\"digest\":\"sha256:"+std::string(64,'a')+
        "\",\"browser_download_url\":\""+nexus::updates::Repository+"/releases/download/"+tag+"/"+asset+"\"}]}]";
}
CURLcode curl_easy_perform(CURL* h){
    assert(h->peer==1&&h->host==2&&h->protocol==CURLPROTO_HTTPS&&!h->follow&&h->signal==1);
    assert(h->ca=="romfs:/certs/cacert.pem"&&h->connect>0&&h->connect<=3000&&h->timeout>0&&h->timeout<=8000);
    assert(mock::sockets&&mock::curl&&mock::romfs);const auto call=++mock::requests;
    mock::timeouts.push_back(h->timeout);mock::performing=true;ON_SCOPE_EXIT(mock::performing=false);
    if(mock::scenario=="cancel"){
        while(!h->progress(h->request,0,0,0,0))std::this_thread::sleep_for(std::chrono::milliseconds(2));return CURLE_FAILED;
    }
    if(mock::scenario=="dns-cancel"||mock::scenario=="dns-timeout"){
        // Simulate synchronous getaddrinfo: curl callbacks/timeouts cannot run.
        while(!mock::dns_cancelled)std::this_thread::sleep_for(std::chrono::milliseconds(2));return CURLE_FAILED;
    }
    if(mock::scenario=="redirect"&&call==1){std::this_thread::sleep_for(std::chrono::milliseconds(70));h->http=302;return CURLE_OK;}
    if(mock::scenario=="http-error"){h->http=503;return CURLE_OK;}
    auto body=mock::scenario=="invalid"?std::string("not json"):Feed();
    if(mock::scenario=="oversized")body.resize(2*1024*1024+1,'a');
    return h->write(body.data(),1,body.size(),h->body)==body.size()?CURLE_OK:CURLE_FAILED;
}
void curl_easy_getinfo(CURL* h,int info,long* value){assert(info==CURLINFO_RESPONSE_CODE);*value=h->http;}
void curl_easy_getinfo(CURL*,int info,char** value){assert(info==CURLINFO_REDIRECT_URL);static char redirect[]="https://objects.githubusercontent.com/asset";*value=redirect;}
const char* curl_easy_strerror(CURLcode){return "transport failed";}
// UPDATE_BACKEND_PRODUCTION
int main(int argc,char** argv){
    assert(argc==2);mock::scenario=argv[1];using namespace nexus::updates;
    if(mock::scenario=="owned-sockets")mock::sockets=true;
    const auto kind=mock::scenario=="new-nsp"?Artifact::Nsp:Artifact::Nro;
    const auto began=std::chrono::steady_clock::now();
    StartStartupCheck(kind);StartStartupCheck(kind);Release release;
    const bool cancel=mock::scenario=="cancel"||mock::scenario=="dns-cancel";
    if(cancel){
        while(!mock::performing)std::this_thread::yield();assert(!TakeStartupRelease(release));
        const auto stopped=std::chrono::steady_clock::now();StopStartupCheck();
        assert(std::chrono::steady_clock::now()-stopped<std::chrono::milliseconds(500));
    }else {
        while(mock::thread_creates && mock::scenario!="thread-create" && mock::scenario!="thread-start" &&
            !startup_check.complete.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(mock::scenario=="completed-stop")StopStartupCheck();
    }
    const bool expected=mock::scenario=="new-nro"||mock::scenario=="new-nsp"||mock::scenario=="redirect"||mock::scenario=="completed-stop";
    const bool taken=TakeStartupRelease(release);
    if(taken!=expected){Release parsed;std::cerr<<mock::scenario<<" requests="<<mock::requests<<" complete="<<startup_check.complete<<" parsed="<<SelectRelease(Feed(),parsed,kind)<<" sockets="<<mock::socket_opens<<" cancel="<<startup_check.cancel<<"\n";}
    assert(taken==expected);assert(!TakeStartupRelease(release));
    StopStartupCheck();StopStartupCheck();const auto opens=mock::socket_opens.load();StartStartupCheck(kind);assert(mock::socket_opens==opens);
    if(expected)assert(release.version=="9.0.0"&&release.kind==kind&&ValidRelease(release));
    assert(!mock::network&&!mock::curl&&!mock::romfs&&!mock::performing);
    if(mock::scenario=="owned-sockets")assert(mock::sockets&&!mock::socket_closes&&!mock::curl_closes);
    else assert(!mock::sockets);
    if(mock::scenario=="dns-timeout"){
        const auto elapsed=std::chrono::steady_clock::now()-began;assert(elapsed>=std::chrono::seconds(3)&&elapsed<std::chrono::seconds(4));
        assert(mock::dns_cancels>0);
    }
    if(mock::scenario=="redirect")assert(mock::timeouts.size()==2&&mock::timeouts[1]<=mock::timeouts[0]-60);
    std::cout<<mock::scenario<<" passed\n";
}
