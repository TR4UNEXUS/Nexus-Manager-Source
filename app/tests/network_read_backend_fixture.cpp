// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <sys/types.h>
using u8=std::uint8_t;using u64=std::uint64_t;
using Result=unsigned;
constexpr Result Module_Libnx=345,LibnxError_IoError=5;
#define MAKERESULT(module,error) (static_cast<Result>(((error)<<9)|(module)))
#define R_UNLESS(condition,result) do{if(!(condition))return (result);}while(false)
namespace nexus {struct Event {std::string phase;Result result;u64 value;};std::vector<Event> events;void Log(const char* phase,Result result=0,u64 value=0){events.push_back({phase,result,value});}}
#define SCOPED_RWLOCK(lock,write)
#define SCOPED_MUTEX(lock)
template<class Callback>struct ScopeExit{Callback call;~ScopeExit(){call();}};
#define ON_SCOPE_EXIT(statement) ScopeExit cleanup{[&]{statement;}}
template<class... Args>void log_write(const char*,Args...){}
struct ReadState {
    size_t maximum{4},offset{},call{};std::vector<int> replies;std::vector<size_t> sizes;
    int reconnect{-1},retrans{2},timeout{60000};bool read_only{};unsigned mounts{};
    int Read(void* buffer,size_t count){
        assert(call<64);sizes.push_back(count);const auto result=call<replies.size()?replies[call]:int(count);++call;
        if(result>0){for(size_t i=0;i<std::min<size_t>(count,result);++i)static_cast<char*>(buffer)[i]='a'+(offset+i)%26;offset+=result;}
        return result;
    }
};
namespace smb {
struct File{void* fd{};};
size_t smb2_get_max_read_size(ReadState* state){return state->maximum;}
int smb2_read(ReadState* state,void*,u8* buffer,size_t count){return state->Read(buffer,count);}
const char* smb2_get_error(ReadState*){return "mock network error";}
struct Device{ReadState* smb2;ssize_t devoptab_read(void*,char*,size_t);};
// SMB_READ_PRODUCTION
}
namespace nfs {
struct Config{std::string url{"nfs://nas/export"};std::map<std::string,std::string> extra;int timeout{10000};bool read_only{true};};
struct File{void* fd{};};
struct nfsdir{};struct Dir{nfsdir* dir{};};
namespace fs {using FsPath=std::string;}
size_t nfs_get_readmax(ReadState* state){return state->maximum;}
int nfs_read(ReadState* state,void*,char* buffer,size_t count){return state->Read(buffer,count);}
const char* nfs_error="mock network error";
const char* nfs_get_error(ReadState*){return nfs_error;}
ReadState context;unsigned initializes{},destroy_urls{},destroy_contexts{},unmounts{},root_opens{},root_closes{};bool fail_initialize{},fail_url{},null_root{};int mount_result{},root_result{};
struct Url{const char* server="nas";const char* path="/export";} url;
ReadState* nfs_init_context(){++initializes;return fail_initialize?nullptr:&context;}
long ini_parse_getl(const char* value,long){return std::strtol(value,nullptr,10);}
void nfs_set_uid(ReadState*,long){}void nfs_set_gid(ReadState*,long){}void nfs_set_version(ReadState*,long){}
void nfs_set_timeout(ReadState* state,int timeout){state->timeout=timeout;}
void nfs_set_readonly(ReadState* state,bool read_only){state->read_only=read_only;}
void nfs_set_autoreconnect(ReadState* state,int value){state->reconnect=value;}
void nfs_set_retrans(ReadState* state,int value){state->retrans=value;}
Url* nfs_parse_url_dir(ReadState*,const char*){return fail_url?nullptr:&url;}
void nfs_destroy_url(Url*){++destroy_urls;}
int nfs_mount(ReadState* state,const char*,const char*){++state->mounts;return mount_result;}
int nfs_get_version(ReadState*){return 3;}
void nfs_destroy_context(ReadState*){++destroy_contexts;}
void nfs_umount(ReadState*){++unmounts;}
int nfs_opendir(ReadState*,const char* path,nfsdir** out){assert(std::string(path)=="/");++root_opens;static nfsdir directory;*out=root_result||null_root?nullptr:&directory;return root_result;}
void nfs_closedir(ReadState*,nfsdir*){++root_closes;}
struct Device{
    ReadState* nfs{};Config config{};bool mounted{};std::string failure_detail;
    Device()=default;explicit Device(ReadState* state):nfs(state){}explicit Device(const Config& c):config(c){}~Device();
    bool Mount();bool CheckRootAccess();bool Fail(const char*,int,const std::string&);const std::string& FailureDetail()const{return failure_detail;}
    ssize_t devoptab_read(void*,char*,size_t);
};
namespace common {
using MountConfig=Config;unsigned registrations{};bool register_result=true;std::unique_ptr<Device> live;
bool MountNetworkSession(std::unique_ptr<Device>&& device,const MountConfig&,size_t,size_t,fs::FsPath& mount){++registrations;if(!register_result)return false;live=std::move(device);mount="nexusnet1:/";return true;}
Result ConnectNfsShare(const MountConfig&,fs::FsPath&,std::string&);
}
std::string NfsFailureMessage(int,bool);
// NFS_READ_PRODUCTION
// NFS_MOUNT_PRODUCTION
// NFS_DIAGNOSTICS_PRODUCTION
}
namespace bridge {
struct _reent{int _errno{};};
struct Backend{off_t result{};unsigned calls{};ssize_t devoptab_seek(void*,off_t,int){++calls;return result;}};
struct Device{Backend* mount_device;};struct File{Device* device;void* fd{};};
int set_errno(_reent* state,int value){state->_errno=value;return -1;}
// SEEK_BRIDGE_PRODUCTION
}
template<class Device,class File>void VerifyRead(){
    char data[64]{};File handle{};
    {ReadState state;Device device{&state};assert(device.devoptab_read(&handle,data,10)==10);assert((state.sizes==std::vector<size_t>{4,4,2}));assert(std::string(data,10)=="abcdefghij");}
    {ReadState state;state.replies={2};Device device{&state};assert(device.devoptab_read(&handle,data,10)==2&&state.call==1);}
    {ReadState state;state.replies={0};Device device{&state};assert(device.devoptab_read(&handle,data,10)==0&&state.call==1);}
    {ReadState state;state.replies={-ETIMEDOUT};Device device{&state};assert(device.devoptab_read(&handle,data,10)==-ETIMEDOUT&&state.call==1);}
    {ReadState state;state.replies={4,-ENOTCONN};Device device{&state};assert(device.devoptab_read(&handle,data,10)==-ENOTCONN&&state.call==2);}
    {ReadState state;state.maximum=0;Device device{&state};assert(device.devoptab_read(&handle,data,10)==-EIO&&state.call==0);assert(device.devoptab_read(&handle,data,0)==0&&state.call==0);}
    {ReadState state;state.replies={5};Device device{&state};assert(device.devoptab_read(&handle,data,10)==-EIO&&state.call==1);}
    {ReadState state;state.replies={4,4,3};Device device{&state};assert(device.devoptab_read(&handle,data,10)==-EIO&&state.call==3);}
}
template<class Device,class File>void VerifyNegotiatedRead(){
    constexpr size_t MiB=1024*1024;File handle{};
    for(size_t limit:{size_t{64*1024},size_t{256*1024},MiB,2*MiB})for(size_t requested:{MiB,MiB+17}){
        ReadState state;state.maximum=limit;Device device{&state};std::vector<char> data(requested);
        assert(device.devoptab_read(&handle,data.data(),requested)==ssize_t(requested));
        assert(state.call==(requested+limit-1)/limit&&state.offset==requested);
        for(size_t i=0;i<state.sizes.size();++i)assert(state.sizes[i]==std::min(limit,requested-i*limit));
        for(size_t i=0;i<requested;++i)assert(data[i]=='a'+i%26);
    }
    // SMB can receive fewer bytes than its advertised max when the server
    // grants fewer credits. Preserve a positive short read and let the caller
    // continue on the same handle, rather than skipping or duplicating bytes.
    ReadState state;state.maximum=MiB;state.replies={64*1024};Device device{&state};std::vector<char> data(MiB);
    const auto first=device.devoptab_read(&handle,data.data(),MiB);assert(first==64*1024);
    assert(device.devoptab_read(&handle,data.data()+first,MiB-first)==ssize_t(MiB-first));
    assert(state.call==2&&state.offset==MiB);for(size_t i=0;i<MiB;++i)assert(data[i]=='a'+i%26);
}
int main(){
    VerifyRead<smb::Device,smb::File>();VerifyRead<nfs::Device,nfs::File>();
    VerifyNegotiatedRead<smb::Device,smb::File>();VerifyNegotiatedRead<nfs::Device,nfs::File>();
    bridge::Backend backend;bridge::Device device{&backend};bridge::File handle{&device};bridge::_reent error;
    for(auto failure:{-EIO,-ETIMEDOUT,-ENOTCONN,-EINVAL}){backend.result=failure;error._errno=0;assert(bridge::devoptab_seek(&error,&handle,4096,SEEK_SET)==-1);assert(error._errno==-failure);}
    for(off_t offset:{off_t{0},off_t{123},off_t{1}<<34}){backend.result=offset;error._errno=EIO;assert(bridge::devoptab_seek(&error,&handle,offset,SEEK_SET)==offset);assert(error._errno==0);}
    for(bool read_only:{false,true}){
        nfs::context={};nfs::initializes=nfs::destroy_urls=0;nfs::Device mount;mount.config.read_only=read_only;
        assert(mount.Mount());assert(nfs::context.timeout==10000&&nfs::context.read_only==read_only);
        assert(nfs::context.reconnect==(read_only?0:-1)&&nfs::context.retrans==(read_only?0:2));
        assert(mount.Mount());assert(nfs::initializes==1&&nfs::context.mounts==1&&nfs::destroy_urls==1);
    }
    nfs::fail_initialize=true;{nfs::Device mount;assert(!mount.Mount());}nfs::fail_initialize=false;
    nfs::fail_url=true;{nfs::Device mount;assert(!mount.Mount());}nfs::fail_url=false;
    nfs::mount_result=-EACCES;{nfs::Device mount;assert(!mount.Mount()&&!mount.mounted);}nfs::mount_result=0;
    // Exercise the actual connection wrapper, including failed mount cleanup,
    // the root access stage, and propagation of a useful reason to the UI.
    const auto connect=[](int failure,const char* stage,const char* phrase){
        nfs::common::registrations=0;nexus::events.clear();const auto old_destroy=nfs::destroy_contexts,old_unmount=nfs::unmounts;
        nfs::fs::FsPath path;std::string detail;const auto result=nfs::common::ConnectNfsShare(nfs::Config{},path,detail);
        assert(result&&path.empty()&&detail.find(phrase)!=detail.npos&&nfs::common::registrations==0);
        assert(nexus::events.back().phase==stage&&nexus::events.back().value==unsigned(failure));
        assert(nfs::destroy_contexts==old_destroy+(nfs::fail_initialize?0:1));
        assert(nfs::unmounts==old_unmount+((!nfs::fail_initialize&&!nfs::fail_url&&!nfs::mount_result)?1:0));
        for(const auto& event:nexus::events)assert(event.phase.find("nas")==event.phase.npos&&event.phase.find("export")==event.phase.npos);
    };
    nfs::fail_initialize=true;connect(ENOMEM,"network-nfs-context","could not create");nfs::fail_initialize=false;
    nfs::fail_url=true;connect(EINVAL,"network-nfs-url","address could not be read");nfs::fail_url=false;
    for(auto [error,phrase]:std::vector<std::pair<int,const char*>>{{EACCES,"denied access"},{ENOENT,"folder was not found"},{ETIMEDOUT,"did not respond"},{1,"Check the server address"}}){nfs::mount_result=-error;connect(error,"network-nfs-mount",phrase);}nfs::mount_result=0;
    nfs::mount_result=-EFAULT;nfs::nfs_error="RPC authentication rejected by server (status 1)";connect(EFAULT,"network-nfs-auth","not the SMB username or password");nfs::mount_result=0;nfs::nfs_error="mock network error";
    for(auto [error,phrase]:std::vector<std::pair<int,const char*>>{{EACCES,"Connected to the NFS server"},{ENOENT,"folder was not found"},{ETIMEDOUT,"did not respond"}}){nfs::root_result=-error;connect(error,"network-nfs-root",phrase);}nfs::root_result=0;
    nfs::null_root=true;connect(EIO,"network-nfs-root","Connected to the NFS server");nfs::null_root=false;
    {nfs::fs::FsPath path;std::string detail="old failure";const auto old_close=nfs::root_closes;nfs::common::register_result=false;assert(nfs::common::ConnectNfsShare(nfs::Config{},path,detail));assert(detail.find("Too many network locations")!=detail.npos&&nfs::root_closes==old_close+1);nfs::common::register_result=true;assert(nfs::common::ConnectNfsShare(nfs::Config{},path,detail)==0&&detail.empty()&&path=="nexusnet1:/");nfs::common::live.reset();}
    {nfs::context={};nfs::Device mount;mount.config.timeout=0;assert(mount.Mount()&&nfs::context.read_only);}
    std::cout<<"Production SMB/NFS reads: negotiated chunks, EOF, short reads, disconnects, zero limits and oversized replies passed; failed seeks propagate -1/errno; read-only NFS disables infinite retries\n";
    std::cout<<"Production NFS connection diagnostics: context, parse, mount, root access, registration, success, errno and cleanup passed without endpoint logging\n";
    std::cout<<"Production SMB/NFS negotiated-read caps: one-MiB requests, 64/256-KiB and one/two-MiB limits, tails, short replies and exact bytes passed\n";
}
