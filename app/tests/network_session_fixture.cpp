// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "nexus/network_browser_model.hpp"
#include "nexus/network_socket_config.hpp"
using Result=unsigned;
using u32=unsigned;
constexpr unsigned Module_Libnx=345,Module_Kernel=1,LibnxError_OutOfMemory=12,KernelError_OutOfMemory=104;
constexpr unsigned LibnxError_AlreadyInitialized=10,LibnxError_TooManyDevOpTabs=11,BsdServiceType_User=1;
#define MAKERESULT(module,error) ((module)|((error)<<9))
#define R_SUCCEEDED(result) (!(result))
#define R_TRY(expression) do {const auto result=(expression);if(result)return result;}while(false)
#define R_UNLESS(expression,result) do {if(!(expression))return (result);}while(false)
namespace nexus {
constexpr Result Unavailable=55;
struct Diagnostic {std::string phase;Result result;std::uint64_t value;};
static std::vector<Diagnostic> diagnostics;
static void Log(const char* phase,Result result=0,std::uint64_t value=0){diagnostics.push_back({phase,result,value});}
}
namespace fs {using FsPath=std::string;}
enum NifmServiceType {NifmServiceType_User};
struct SocketInitConfig {
    u32 tcp_tx_buf_size{},tcp_rx_buf_size{},tcp_tx_buf_max_size{},tcp_rx_buf_max_size{};
    u32 udp_tx_buf_size{},udp_rx_buf_size{},sb_efficiency{},num_bsd_sessions{3},bsd_service_type{1};
};
struct BsdInitConfig {
    u32 version{},tcp_tx_buf_size{},tcp_rx_buf_size{},tcp_tx_buf_max_size{},tcp_rx_buf_max_size{};
    u32 udp_tx_buf_size{},udp_rx_buf_size{},sb_efficiency{};
};
// LIBNX_DEFAULT_CONFIG_PRODUCTION
// LIBNX_POOL_SIZE_PRODUCTION
static const int g_socketDevoptab{};
static unsigned g_bsdResult{},g_bsdErrno{};
static bool socket_device{},bsd_open{},nifm_open{},mount_open{},fail_add{};
static Result bsd_result{},nifm_result{},address_result{};
static std::vector<Result> bsd_results;
static std::vector<BsdInitConfig> bsd_configs;
static u32 address=1;
static std::vector<std::string> events;
static unsigned socketSelectVersion(){return 9;}
static int FindDevice(const char* name){assert(std::strcmp(name,"soc:")==0);return socket_device?1:-1;}
static int AddDevice(const int*){if(fail_add)return -1;socket_device=true;events.emplace_back("socket-add");return 1;}
static void RemoveDevice(const char* name){assert(std::strcmp(name,"soc:")==0);socket_device=false;events.emplace_back("socket-remove");}
static Result bsdInitialize(const BsdInitConfig* config,u32 sessions,u32 service){
    assert(!bsd_open);assert(sessions==g_defaultSocketInitConfig.num_bsd_sessions&&service==g_defaultSocketInitConfig.bsd_service_type);
    bsd_configs.push_back(*config);Result result=bsd_result;
    if(!bsd_results.empty()){result=bsd_results.front();bsd_results.erase(bsd_results.begin());}
    if(result)return result;
    bsd_open=true;events.emplace_back("bsd-open");return 0;
}
static void bsdExit(){bsd_open=false;events.emplace_back("bsd-close");}
void socketExit();
// LIBNX_SOCKET_PRODUCTION
static Result nifmInitialize(NifmServiceType){assert(socket_device&&bsd_open);if(nifm_result)return nifm_result;nifm_open=true;events.emplace_back("nifm-open");return 0;}
static void nifmExit(){assert(nifm_open&&socket_device&&!mount_open);nifm_open=false;events.emplace_back("nifm-close");}
static Result nifmGetCurrentIpAddress(u32* out){assert(nifm_open);*out=address;return address_result;}
static void UmountNeworkDevice(const fs::FsPath&){assert(socket_device&&bsd_open&&nifm_open&&mount_open);mount_open=false;events.emplace_back("unmount");}
// NETWORK_SESSION_PRODUCTION

static void Clean(){assert(!socket_device&&!bsd_open&&!nifm_open&&!mount_open);}
static void Mounted(Session& session){session.mount="nexusnet1:/";mount_open=true;}
static void ExpectedError(Result expected,const std::string& phase,const std::string& message){
    nexus::diagnostics.clear();std::string detail="stale detail";
    {Session session;session.name="private-server-name";assert(session.Open(&detail)==expected);}
    Clean();assert(!detail.empty()&&detail.find(message)!=std::string::npos);
    assert(!nexus::diagnostics.empty());
    assert(nexus::diagnostics.back().phase==phase&&nexus::diagnostics.back().result==expected);
    for(const auto& event:nexus::diagnostics)assert(event.phase.find("private-server-name")==std::string::npos);
}
int main(){
    const auto defaults=*socketGetDefaultInitConfig();
    const auto tuned=nexus::network::DownloadSocketConfig(defaults);
    assert(tuned.tcp_rx_buf_size==512*1024&&tuned.tcp_rx_buf_max_size==1024*1024);
    assert(tuned.tcp_tx_buf_size==defaults.tcp_tx_buf_size&&tuned.tcp_tx_buf_max_size==defaults.tcp_tx_buf_max_size);
    assert(tuned.udp_tx_buf_size==defaults.udp_tx_buf_size&&tuned.udp_rx_buf_size==defaults.udp_rx_buf_size);
    assert(tuned.sb_efficiency==defaults.sb_efficiency&&tuned.num_bsd_sessions==defaults.num_bsd_sessions&&tuned.bsd_service_type==defaults.bsd_service_type);
    assert(nexus::network::SocketTransferMemorySize(defaults)==2310144);
    assert(nexus::network::SocketTransferMemorySize(tuned)==5455872);
    assert(nexus::network::SocketTransferMemorySize(tuned)-nexus::network::SocketTransferMemorySize(defaults)==3*1024*1024);
    // Repeated ordinary browser Back must release mount before sockets so a
    // subsequent saved shortcut can create a completely fresh connection.
    for(unsigned i=0;i<100;++i){
        events.clear();
        {Session session;std::string detail="stale detail";assert(!session.Open(&detail));assert(detail.empty());Mounted(session);}
        Clean();
        const std::vector<std::string> expected{"bsd-open","socket-add","nifm-open","unmount","nifm-close","socket-remove","bsd-close"};
        assert(events==expected);
        const auto& actual=bsd_configs.back();
        assert(actual.tcp_rx_buf_size==512*1024&&actual.tcp_rx_buf_max_size==1024*1024);
        assert(_bsdGetTransferMemSizeForConfig(&actual)==nexus::network::SocketTransferMemorySize(actual));
    }
    // A mount/parse failure has no registered mount; normal Session cleanup is
    // still sufficient for a retry, including one immediately afterward.
    {Session session;assert(!session.Open());}Clean();
    bsd_result=101;ExpectedError(101,"network-sockets","sockets");bsd_result=0;
    fail_add=true;ExpectedError(MAKERESULT(Module_Libnx,LibnxError_TooManyDevOpTabs),"network-sockets","sockets");fail_add=false;
    nifm_result=102;ExpectedError(102,"network-nifm","network service");nifm_result=0;
    address_result=103;ExpectedError(103,"network-ip","network address");address_result=0;
    address=0;ExpectedError(nexus::Unavailable,"network-ip","network address");address=1;
    {Session session;assert(!session.Open());Mounted(session);}Clean();

    for(const auto error:{MAKERESULT(Module_Libnx,LibnxError_OutOfMemory),MAKERESULT(Module_Kernel,KernelError_OutOfMemory)}){
        bsd_configs.clear();bsd_results={error,0};nexus::diagnostics.clear();
        {Session session;std::string detail;assert(!session.Open(&detail));assert(detail.empty());Mounted(session);}
        Clean();assert(bsd_results.empty()&&bsd_configs.size()==2);
        assert(bsd_configs[0].tcp_rx_buf_max_size==1024*1024&&bsd_configs[1].tcp_rx_buf_size==defaults.tcp_rx_buf_size&&bsd_configs[1].tcp_rx_buf_max_size==defaults.tcp_rx_buf_max_size);
        assert(_bsdGetTransferMemSizeForConfig(&bsd_configs[1])==2310144);
        assert(nexus::diagnostics.front().phase=="network-sockets-memory-fallback"&&nexus::diagnostics.front().result==error);
        bool pool_seen=false;for(const auto& event:nexus::diagnostics)if(event.phase=="network-socket-pool"){pool_seen=true;assert(event.value==2310144);}
        assert(pool_seen);
    }
    bsd_results={MAKERESULT(Module_Libnx,LibnxError_OutOfMemory),109};ExpectedError(109,"network-sockets","sockets");assert(bsd_results.empty());

    // libnx sockets are singleton rather than reference counted. A duplicate
    // attempt fails without owning or closing the first Session's socket stack.
    {
        Session first;assert(!first.Open());Mounted(first);
        const auto attempts=bsd_configs.size();
        {Session second;std::string detail;assert(second.Open(&detail)==MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized));assert(detail.find("Another network connection")!=std::string::npos);}
        assert(bsd_configs.size()==attempts);
        assert(socket_device&&bsd_open&&nifm_open&&mount_open);
    }Clean();

    // Match BrowserModel's real ownership shape: rows and refresh retain the
    // Session, while all navigation references back to the Model are weak.
    auto session=std::make_shared<Session>();assert(!session->Open());Mounted(*session);
    auto browser=std::make_shared<nexus::network::browser::Model>();
    std::weak_ptr<Session> session_observer=session;
    std::weak_ptr<nexus::network::browser::Model> weak=browser;
    browser->Replace("Browse NFS","Nas1:/",{{"Folder","Folder",nexus::network::browser::Kind::Folder,
        [session,weak]{assert(session->sockets);assert(!weak.expired());}}},"Help","Empty",{},
        [session,weak]{assert(session->sockets);assert(!weak.expired());});
    session.reset();assert(!session_observer.expired());
    browser->Activate();browser->refresh();browser.reset();
    assert(session_observer.expired()&&weak.expired());Clean();
    std::cout<<"Production Session + libnx socket initialization: bounded RX/pool sizes, OOM-only fallback, 100 reopen cycles, partial failures, duplicate ownership, cleanup and weak browser ownership passed\n";
}
