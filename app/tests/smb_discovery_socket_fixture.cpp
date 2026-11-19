// Compile the real discovery loop against a scripted LAN, with no real network I/O.
#include "nexus/smb_discovery_core.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cassert>
#include <iostream>
#include <map>
#include <string>

using u8=std::uint8_t;using u32=std::uint32_t;using Result=u32;
#define R_TRY(x) do {const auto r_=(x);if(r_)return r_;}while(false)
#define R_UNLESS(x,r) do {if(!(x))return r;}while(false)
#define R_FAILED(x) ((x)!=0)
#define MAKERESULT(a,b) (Result(b))
constexpr Result Module_Libnx=1,LibnxError_IoError=3,Cancel=4;
namespace fixture {
namespace c=nexus::smbdiscovery;
enum class Mode {Valid,Immediate,Timeout,Refused,Bad,EndOfFile,SendZero,ReadSilent,ConnectError,LateReply,FirstConnectTimeout,FirstClose};
struct Socket {bool nfs{};Mode mode{};bool connected{},send_again{},read_again{},tx_buffer{},rx_buffer{};c::Bytes sent,reply;size_t read{};long sent_at{};};
inline std::map<u32,Mode> targets,smb_targets;inline std::map<int,Socket> sockets;
inline std::map<std::pair<u32,bool>,size_t> attempts;
inline std::map<std::string,size_t> logs;
inline long elapsed{},cancel_at{-1},change_at{-1},pause_at{-1};
inline size_t opened{},closed{},maximum{},reported{},expected_hosts{};
inline int next_fd{100};inline bool fail_socket{},fail_buffers{},fail_flags{},fail_poll{},poll_eintr{},nifm_failure{};
inline u32 address{0xc0a80105},mask{0xffffff00};inline size_t send_chunk{11},read_chunk{17};
static c::Bytes Reply(){
    c::Bytes b(132);b[3]=128;b[4]=0xfe;b[5]='S';b[6]='M';b[7]='B';
    c::Put16(b,8,64);c::Put32(b,20,1);c::Put16(b,68,65);c::Put16(b,72,0x0302);return b;
}
static void Reset(){
    assert(sockets.empty());targets.clear();smb_targets.clear();attempts.clear();logs.clear();elapsed=opened=closed=maximum=reported=expected_hosts=0;cancel_at=change_at=pause_at=-1;
    fail_socket=fail_buffers=fail_flags=fail_poll=poll_eintr=nifm_failure=false;address=0xc0a80105;mask=0xffffff00;next_fd=100;send_chunk=11;read_chunk=17;
}
static int SocketOpen(int family,int type,int protocol){
    assert(family==AF_INET&&type==SOCK_STREAM&&protocol==IPPROTO_TCP);
    if(fail_socket){errno=EMFILE;return -1;}
    const int fd=next_fd++;sockets.emplace(fd,Socket{});++opened;maximum=std::max(maximum,sockets.size());return fd;
}
static int Close(int fd){assert(sockets.erase(fd)==1);++closed;return 0;}
static int SetOption(int fd,int level,int option,const void* value,socklen_t size){
    assert(level==SOL_SOCKET&&size==sizeof(int)&&*static_cast<const int*>(value)==16*1024);
    if(fail_buffers){errno=ENOMEM;return -1;}
    auto& s=sockets.at(fd);if(option==SO_SNDBUF)s.tx_buffer=true;else {assert(option==SO_RCVBUF);s.rx_buffer=true;}return 0;
}
static int Flags(int fd,int command,int flags){
    assert(sockets.contains(fd));if(fail_flags){errno=EIO;return -1;}
    if(command==F_GETFL)return 0;assert(command==F_SETFL&&(flags&O_NONBLOCK));return 0;
}
static int Connect(int fd,const sockaddr* endpoint,socklen_t size){
    assert(size==sizeof(sockaddr_in));const auto& ep=*reinterpret_cast<const sockaddr_in*>(endpoint);
    const auto ip=ntohl(ep.sin_addr.s_addr);assert(ep.sin_family==AF_INET&&(ntohs(ep.sin_port)==445||ntohs(ep.sin_port)==2049));
    c::Range range;assert(c::Plan(address,mask,range));assert(range.hosts.contains(ip));
    auto& s=sockets.at(fd);assert(s.tx_buffer&&s.rx_buffer);s.nfs=ntohs(ep.sin_port)==2049;
    const auto& services=!s.nfs&&smb_targets.contains(ip)?smb_targets:targets;
    const auto it=services.find(ip);s.mode=it==services.end()?Mode::Timeout:it->second;
    const auto attempt=++attempts[{ip,s.nfs}];
    if(s.mode==Mode::FirstConnectTimeout)s.mode=attempt==1?Mode::Timeout:Mode::Valid;
    if(s.mode==Mode::FirstClose)s.mode=attempt==1?Mode::EndOfFile:Mode::Valid;
    s.reply=Reply();if(s.nfs){s.reply.assign(28,0);c::PutBe32(s.reply,0,0x80000018);c::PutBe32(s.reply,4,0x42424242);c::PutBe32(s.reply,8,1);}
    if(s.mode==Mode::Bad)s.reply[4]='H';
    if(s.mode==Mode::Immediate){s.connected=true;return 0;}
    if(s.mode==Mode::Refused){errno=ECONNREFUSED;return -1;}
    errno=EINPROGRESS;return -1;
}
static int Poll(pollfd* descriptors,nfds_t count,int timeout){
    assert(timeout<=50&&count<=c::MaxPeers);elapsed+=timeout;
    if(fail_poll){errno=EIO;return -1;}if(poll_eintr){poll_eintr=false;errno=EINTR;return -1;}
    int events=0;
    for(size_t i=0;i<count;++i){auto& p=descriptors[i];p.revents=0;if(p.fd<0)continue;auto& s=sockets.at(p.fd);
        if(s.mode==Mode::Timeout)continue;
        if(!s.connected||s.sent.size()<(s.nfs?44u:162u)){p.revents=POLLOUT;++events;}
        else if(s.mode!=Mode::ReadSilent&&!(s.mode==Mode::LateReply&&elapsed-s.sent_at<2400)){p.revents=POLLIN;if(s.read+read_chunk>=s.reply.size())p.revents|=POLLHUP;++events;}
    }
    return events;
}
static int Error(int fd,int level,int option,void* value,socklen_t* size){
    assert(level==SOL_SOCKET&&option==SO_ERROR&&*size==sizeof(int));auto& s=sockets.at(fd);
    *static_cast<int*>(value)=s.mode==Mode::ConnectError?ECONNREFUSED:0;s.connected=true;return 0;
}
static ssize_t Send(int fd,const void* bytes,size_t size,int){
    auto& s=sockets.at(fd);assert(s.connected);if(s.mode==Mode::SendZero)return 0;
    if(!s.send_again){s.send_again=true;errno=EAGAIN;return -1;}
    const auto count=std::min(size,send_chunk);const auto p=static_cast<const u8*>(bytes);s.sent.insert(s.sent.end(),p,p+count);
    assert(s.sent.size()<=(s.nfs?44u:162u));if(s.sent.size()==(s.nfs?44u:162u))s.sent_at=elapsed;
    if(s.nfs&&s.sent.size()==44){assert(c::Be32(s.sent,4)==0x42424242&&c::Be32(s.sent,16)==100003&&c::Be32(s.sent,20)==3);}
    if(!s.nfs&&s.sent.size()==162){assert(s.sent[3]==158&&s.sent[4]==0xfe&&c::Get16(s.sent,16)==0&&c::Get16(s.sent,68)==36);}
    return count;
}
static ssize_t Read(int fd,void* bytes,size_t size,int){
    auto& s=sockets.at(fd);assert(s.sent.size()==(s.nfs?44u:162u));if(s.mode==Mode::EndOfFile)return 0;
    if(!s.read_again){s.read_again=true;errno=EINTR;return -1;}
    const auto count=std::min({size,read_chunk,s.reply.size()-s.read});std::memcpy(bytes,s.reply.data()+s.read,count);s.read+=count;return count;
}
}
struct FakeClock {
    using time_point=std::chrono::steady_clock::time_point;
    static auto now(){return time_point(std::chrono::milliseconds(fixture::elapsed));}
};
static Result nifmGetCurrentIpConfigInfo(u32* address,u32* mask,u32*,u32*,u32*){
    if(fixture::nifm_failure)return 8;
    *address=htonl(fixture::address+((fixture::change_at>=0&&fixture::elapsed>=fixture::change_at)?1:0));*mask=htonl(fixture::mask);return 0;
}
static void randomGet(void* p,size_t n){std::memset(p,0x42,n);}
namespace nexus {
constexpr Result Unavailable=1,Invalid=2;
void Log(const char* phase,Result,size_t value){fixture::logs[phase]=value;}
namespace lifecycle {bool CanPoll(){return fixture::pause_at<0||fixture::elapsed<fixture::pause_at;}}
namespace transfer {enum class Mode {Off};Mode Active(){return Mode::Off;}}
}
namespace sphaira::ui {
struct ProgressBox {
    bool ShouldExit(){return ShouldExitResult()!=0;}
    Result ShouldExitResult(){return fixture::cancel_at>=0&&fixture::elapsed>=fixture::cancel_at?Cancel:0;}
    void SetProgressNote(const std::string&){}
    void SetOverallTransfer(size_t done,size_t total,const std::string&){assert(done<=total);fixture::reported=done;fixture::expected_hosts=total;}
};
}
namespace sphaira::nexusapp {struct SmbServer {std::string host,address,label,protocol;};}
#define socket fixture::SocketOpen
#define close fixture::Close
#define fcntl fixture::Flags
#define connect fixture::Connect
#define poll fixture::Poll
#define getsockopt fixture::Error
#define setsockopt fixture::SetOption
#define send fixture::Send
#define recv fixture::Read
// DISCOVERY_PRODUCTION
#undef socket
#undef close
#undef fcntl
#undef connect
#undef poll
#undef getsockopt
#undef setsockopt
#undef send
#undef recv

int main(){
    using namespace fixture;using namespace sphaira::nexusapp;
    std::vector<SmbServer> found;std::string detail;sphaira::ui::ProgressBox progress;
    const auto run=[&]{auto rc=DiscoverSmbServers(&progress,found,detail);assert(sockets.empty()&&closed==opened&&maximum<=c::MaxPeers);return rc;};
    Reset();targets={{0xc0a80101,Mode::Valid},{0xc0a80102,Mode::Immediate},{0xc0a80103,Mode::Bad},{0xc0a80104,Mode::EndOfFile},{0xc0a80106,Mode::Refused},{0xc0a80107,Mode::SendZero},{0xc0a80108,Mode::ReadSilent},{0xc0a80109,Mode::ConnectError}};
    poll_eintr=true;assert(run()==0&&found.size()==2&&elapsed<=30000&&expected_hosts==256&&reported==256);
    assert(found[0].host=="192.168.1.1"&&found[1].host=="192.168.1.2"&&found[0].address==found[0].host);
    assert(detail.find("192.168.1.0/24")!=std::string::npos&&detail.find("256 of 256")!=std::string::npos);
    Reset();mask=0xfffffe00;assert(run()==0&&found.empty()&&detail.find("509 of 509")!=std::string::npos);
    Reset();address=0xc0a80109;mask=0xfffffffc;targets={{0xc0a8010a,Mode::Valid}};assert(run()==0&&found.size()==1&&detail.find("/30")!=std::string::npos);
    Reset();targets={{0xc0a80101,Mode::Valid}};send_chunk=162;read_chunk=4096;cancel_at=250;
    assert(run()==0&&found.size()==1&&detail.find("stopped early")!=std::string::npos&&elapsed<=300); // cancellation keeps confirmed servers for browsing
    Reset();targets={{0xc0a80101,Mode::Valid}};send_chunk=162;read_chunk=4096;change_at=1000;
    assert(run()==nexus::Unavailable&&found.empty()&&detail.find("connection changed")!=std::string::npos);
    Reset();pause_at=250;assert(run()==nexus::Unavailable&&found.empty()&&elapsed<=300);
    Reset();fail_socket=true;assert(run()==LibnxError_IoError&&opened==0&&found.empty());
    Reset();fail_buffers=true;assert(run()==LibnxError_IoError&&opened==253&&closed==opened&&attempts.empty());
    Reset();fail_flags=true;assert(run()==LibnxError_IoError&&opened==253&&found.empty());
    Reset();fail_poll=true;assert(run()==LibnxError_IoError&&found.empty());
    Reset();nifm_failure=true;assert(run()==8&&opened==0&&found.empty());
    Reset();address=0x08080808;assert(run()==nexus::Unavailable&&opened==0&&found.empty());
    Reset();for(u32 i=1;i<255;++i)targets.emplace(0xc0a80100|i,Mode::ReadSilent);
    send_chunk=162;assert(run()==0&&found.empty()&&elapsed>15000&&reported==506&&detail.find("506 of 506")!=std::string::npos);
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Valid}};
    assert(DiscoverNetworkServers(&progress,found,detail,"")==0&&found.size()==2&&reported==2&&closed==opened);
    assert(found[0].protocol!=found[1].protocol);
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Valid}};
    assert(DiscoverNetworkServers(&progress,found,detail,"NFS")==0&&found.size()==1&&found[0].protocol=="NFS"&&reported==1&&closed==opened);
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Bad}};
    assert(DiscoverNetworkServers(&progress,found,detail,"NFS")==0&&found.empty()&&closed==opened);
    // A healthy NFS service must trigger a second, independently validated SMB
    // attempt when that host's first SMB TCP connection timed out.
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Valid}};smb_targets={{0xc0a8010a,Mode::FirstConnectTimeout}};
    send_chunk=162;read_chunk=4096;
    assert(DiscoverNetworkServers(&progress,found,detail,"")==0&&found.size()==2&&reported==3&&closed==opened);
    assert(found[0].protocol=="NFS"&&found[1].protocol=="SMB"&&found[0].host==found[1].host);
    assert(logs["discovery-smb-retried"]==1&&logs["discovery-smb-found"]==1&&logs["discovery-smb-timeouts"]==1);
    // Slow SMB negotiation is retried once with a fresh socket and longer wait.
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::LateReply}};send_chunk=162;read_chunk=4096;
    assert(run()==0&&found.size()==1&&reported==2&&logs["discovery-smb-retried"]==1);
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::FirstClose}};send_chunk=162;read_chunk=4096;
    assert(run()==0&&found.size()==1&&logs["discovery-smb-closed"]==1&&logs["discovery-smb-retried"]==1);
    // Do not label a non-SMB port as SMB just because its NFS service works.
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Valid}};smb_targets={{0xc0a8010a,Mode::Bad}};
    assert(DiscoverNetworkServers(&progress,found,detail,"")==0&&found.size()==1&&found[0].protocol=="NFS");
    assert(logs["discovery-smb-found"]==0&&logs["discovery-smb-invalid"]==2&&logs["discovery-smb-retried"]==1);
    // Cancelling the retry leaves the confirmed NFS result available to browse.
    Reset();mask=0xfffffffc;address=0xc0a80109;targets={{0xc0a8010a,Mode::Valid}};smb_targets={{0xc0a8010a,Mode::FirstConnectTimeout}};
    send_chunk=162;read_chunk=4096;cancel_at=1000;
    assert(DiscoverNetworkServers(&progress,found,detail,"")==0&&found.size()==1&&found[0].protocol=="NFS"&&closed==opened);
    assert(detail.find("stopped early")!=std::string::npos);
    std::cout<<"SMB discovery scripted socket/cancellation/deadline tests passed\n";
}
