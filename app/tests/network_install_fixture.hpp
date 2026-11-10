#include "nexus/nx_Policy.hpp"
#include "nexus/install_diagnostics.hpp"
#include "nexus/file_manager_core.hpp"
#include "nexus/package.hpp"
#include <sys/stat.h>
#include <functional>
#include <optional>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
using u8=std::uint8_t;using u64=std::uint64_t;using s64=std::int64_t;using Result=std::uint32_t;
inline constexpr unsigned Module_Libnx=345,LibnxError_IoError=17;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_TRY(call) do{const Result result_=(call);if(result_)return result_;}while(0)
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(0)
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
namespace fs {struct FsPath{std::string path;explicit FsPath(const char* p):path(p){}explicit FsPath(const std::string& p):path(p){}};}
struct State {
    struct stat path_info{},handle_info{};
    bool applet{},foreground{true},transfer{},mounted{true},readonly{true},nostat{},begin_replace{},zero{},oversize{};
    Result usb_error{},physical_error{},destination_error{},pending_error{},mount_error{};
    int lstat_error{},handle_error{},read_error{},data_error{};
    u64 generation{42};size_t short_read{SIZE_MAX},max_request{},source_reads{},opened{},closed{},installs{},destination_queries{};
    size_t usb_constructions{},usb_destructions{};
    std::vector<std::pair<size_t,u64>> usb_checks;
    std::vector<std::pair<u64,size_t>> read_requests;
    size_t published{1},skipped{};
    std::function<void()> after_read;
    std::vector<u8> bytes;
    nexus::Target destination{nexus::Target::Internal};
} state;
int fake_lstat(const char*,struct stat* out){if(state.lstat_error){errno=state.lstat_error;return -1;}*out=state.path_info;return 0;}
#define lstat fake_lstat
namespace sphaira {
struct SavedBool{bool value{true};bool Get()const{return value;}};
struct SavedFilters{SavedBool m_ticket_only,m_skip_base,m_skip_patch,m_skip_addon,m_skip_data_patch,m_skip_ticket;} saved_filters;
struct App{static bool IsApplet(){return state.applet;}static SavedFilters* GetApp(){return &saved_filters;}};
namespace ui {struct ProgressBox{Result cancel{};std::string note;bool network_active{};u64 network_received{},network_begins{},network_ends{};
Result ShouldExitResult(){return cancel;}void SetProgressNote(const std::string& s){note=s;}
void BeginNetworkTransfer(){network_active=true;network_received=0;++network_begins;}
void AddNetworkBytes(u64 bytes){assert(network_active);network_received+=bytes;}
void EndNetworkTransfer(){network_active=false;++network_ends;}};}
namespace location {
enum FsEntryFlag{FsEntryFlag_ReadOnly=1,FsEntryFlag_NoStatFile=8};
struct StdioEntry{std::string mount;unsigned flags;};using StdioEntries=std::vector<StdioEntry>;
}
namespace devoptab {
Result GetNetworkDevices(location::StdioEntries& out){out.clear();if(state.mount_error)return state.mount_error;if(state.mounted)out.push_back({"nexusnet1:/",(state.readonly?1u:0u)|(state.nostat?8u:0u)});return 0;}
}
namespace nexusapp {
// NETWORK_OUTCOME_PRODUCTION
}
namespace yati {
namespace source {struct Base{virtual ~Base()=default;virtual Result Read(void*,s64,s64,u64*)=0;virtual void SignalCancel(){};virtual bool IsStream()const{return false;}Result m_open_result{};Result GetOpenResult(){return m_open_result;}};}
struct InstallOutcome{size_t installed_records{},skipped_records{};};
struct Config{bool ticket_only{},skip_base{},skip_patch{},skip_addon{},skip_data_patch{},skip_ticket{};};
struct ConfigOverride{
    bool quick_usb_final_verify{};
    std::optional<nexus::Target> nexus_target;u64 nexus_destination_generation{};nexus::install::Diagnostic* diagnostic{};void* receipt{};
    std::optional<bool> skip_nca_hash_verify,convert_to_standard_crypto,lower_master_key,lower_system_version,ignore_distribution_bit;
    std::optional<bool> ticket_only,skip_base,skip_patch,skip_addon,skip_data_patch,skip_ticket;InstallOutcome* outcome{};
};
Config ResolveFilters(const ConfigOverride& override){Config config{};
// YATI_FILTER_SETUP_PRODUCTION
return config;}
std::string observed_source_path;ConfigOverride observed_config;std::function<Result(source::Base&,ui::ProgressBox*)> install;
Result InstallFromSource(ui::ProgressBox* p,source::Base* source,const fs::FsPath& name,const ConfigOverride& config,s64 size){
    ++state.installs;observed_config=config;observed_source_path=name.path;assert(nexus::package::Supported(name.path)&&size==state.path_info.st_size&&!source->IsStream());
    const auto effective=ResolveFilters(config);assert(!effective.ticket_only&&!effective.skip_base&&!effective.skip_patch&&!effective.skip_addon&&!effective.skip_data_patch&&!effective.skip_ticket);
    config.diagnostic->Clear();const auto result=install?install(*source,p):0;
    if(!result&&config.outcome)*config.outcome={state.published,state.skipped};return result;
}
}
}
namespace nexus {
inline constexpr Result Invalid=123,Unavailable=124;
struct Space{u64 free{1ULL<<40},total{2ULL<<40};};
struct Client{
    const size_t identity{++state.usb_constructions};
    ~Client(){++state.usb_destructions;}
    Result Check(u64 generation){state.usb_checks.emplace_back(identity,generation);return state.usb_error?state.usb_error:generation==state.generation?0:Unavailable;}
};
Result CheckUsbMutationAllowed(){return state.pending_error;}
Result QueryPhysicalSd(Space&){return state.physical_error;}
Result QueryDestination(Target target,Space&){++state.destination_queries;state.destination=target;return state.destination_error;}
namespace lifecycle {bool CanPoll(){return state.foreground;}}
namespace transfer {enum class Mode{Off,Cable,Ftp};Mode Active(){return state.transfer?Mode::Cable:Mode::Off;}}
namespace network {
class Source {
    std::function<bool()> check;bool open{};std::string pinned;
public:
    Source(std::string,std::function<bool()> c):check(std::move(c)){}
    ~Source(){if(open)++state.closed;}
    int BeginRead(const std::string& path){if(!check())return ECANCELED;if(open)return pinned==path?0:EBUSY;open=true;pinned=path;++state.opened;if(state.begin_replace)++state.path_info.st_ino;state.handle_info=state.path_info;return 0;}
    int StatOpen(struct stat& out){if(!check())return ECANCELED;if(!open)return EBADF;if(state.handle_error)return state.handle_error;out=state.handle_info;return 0;}
    int Read(const std::string& path,u64 off,void* data,size_t count,u64& actual){
        actual=0;if(!check())return ECANCELED;assert(open&&path==pinned);++state.source_reads;state.max_request=std::max(state.max_request,count);
        state.read_requests.emplace_back(off,count);
        if(state.read_error)return state.read_error;if(state.zero)return 0;if(state.oversize){actual=count+1;return 0;}
        if(off>state.bytes.size())return EIO;actual=std::min<u64>({count,state.short_read,state.bytes.size()-off});memcpy(data,state.bytes.data()+off,actual);
        if(state.after_read)state.after_read();return state.data_error;
    }
};
}
}
void Reset(size_t bytes=700000){state={};state.path_info.st_mode=S_IFREG|0444;state.path_info.st_size=bytes;state.path_info.st_ino=9;state.path_info.st_dev=3;state.path_info.st_mtime=12;state.path_info.st_ctime=13;state.bytes.resize(state.path_info.st_size);for(size_t i=0;i<state.bytes.size();++i)state.bytes[i]=u8(i*31);sphaira::yati::install={};}
