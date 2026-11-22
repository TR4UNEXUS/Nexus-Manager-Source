#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/smb_discovery.hpp"
#include "nexus/smb_discovery_core.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "ui/progress_box.hpp"
#include "defines.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <bit>
#include <chrono>
#include <cstring>

namespace sphaira::nexusapp {
namespace {
namespace core=nexus::smbdiscovery;
using Clock=std::chrono::steady_clock;
constexpr auto ConnectWait=std::chrono::milliseconds(900),ReplyWait=std::chrono::milliseconds(1800);
constexpr auto RetryConnectWait=std::chrono::milliseconds(3000),RetryReplyWait=std::chrono::milliseconds(5000);
constexpr size_t MaxRetries=1024;
struct Probe {
    u32 ip{};bool nfs{};
    bool operator==(const Probe&)const=default;
    bool operator<(const Probe& other)const{return ip!=other.ip?ip<other.ip:nfs<other.nfs;}
};
struct Statistics {size_t found{},timeouts{},reply_timeouts{},connect_errors{},invalid{},closed{},retried{};};
enum class Stage {Connect,Send,Read};
struct Peer {
    int fd{-1};u32 ip{};bool nfs{};Stage stage{};size_t sent{};Clock::time_point deadline{};core::Bytes reply;
    ~Peer(){Close();}
    void Close(){if(fd>=0)close(fd);fd=-1;reply.clear();sent=0;}
};
bool Again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
std::string Address(u32 ip){in_addr a{};a.s_addr=htonl(ip);char text[INET_ADDRSTRLEN]{};return inet_ntop(AF_INET,&a,text,sizeof(text))?text:std::string{};}
Result Network(u32& address,u32& mask){u32 gateway{},dns1{},dns2{};R_TRY(nifmGetCurrentIpConfigInfo(&address,&mask,&gateway,&dns1,&dns2));return address&&mask?0:nexus::Unavailable;}
Result Check(ui::ProgressBox* p){R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);return 0;}
}
Result DiscoverNetworkServers(ui::ProgressBox* progress,std::vector<SmbServer>& result,std::string& detail,const std::string& protocol){
    R_UNLESS(protocol.empty()||protocol=="SMB"||protocol=="NFS",nexus::Invalid);
    result.clear();detail="Network discovery did not finish. No shares or files were opened."_nx;
    R_TRY(Check(progress));u32 address{},mask{};R_TRY(Network(address,mask));core::Range range;
    if(!core::Plan(ntohl(address),ntohl(mask),range)){
        detail="Automatic discovery needs a private IPv4 local network. Enter the server address manually for this connection."_nx;return nexus::Unavailable;
    }
    std::array<u8,16> guid{};std::array<u8,32> salt{};randomGet(guid.data(),guid.size());randomGet(salt.data(),salt.size());
    guid[6]=(guid[6]&15)|0x40;guid[8]=(guid[8]&63)|0x80;
    const auto smb_request=core::Negotiate(guid,salt);const auto xid=core::Get32(salt,0);const auto nfs_request=core::NfsNull(xid);
    const size_t per_host=protocol.empty()?2:1,initial_total=range.hosts.size()*per_host;size_t total=initial_total;
    std::array<Peer,core::MaxPeers> peers;std::vector<Probe> retries,confirmed;std::array<Statistics,2> statistics{};
    const auto start=Clock::now();auto network_check=start;size_t finished{},socket_errors{},opened{};bool stopped=false,retry_limit=false;
    progress->SetProgressNote("Scanning the full connected subnet for servers. B stops and shows servers found so far."_nx);
    const auto failure=[&](Result rc){result.clear();return rc;};
    const auto remember=[&](Probe probe){
        if(std::find(retries.begin(),retries.end(),probe)!=retries.end())return;
        if(retries.size()<MaxRetries)retries.push_back(probe);else retry_limit=true;
    };
    // Recheck reachable servers once, after the broad scan has released its
    // sockets. An NFS reply is a reason to retry SMB, never proof of SMB itself.
    for(unsigned pass=0;pass<2&&!stopped;++pass){
    if(pass){
        std::erase_if(retries,[&](const auto& probe){return std::find(confirmed.begin(),confirmed.end(),probe)!=confirmed.end();});
        std::sort(retries.begin(),retries.end());total+=retries.size();
        if(retries.empty())break;
        progress->SetProgressNote("Rechecking services on reachable servers. B stops and keeps servers already found."_nx);
    }
    size_t next{};const auto batch_total=pass?retries.size():initial_total;
    const auto connect_wait=pass?RetryConnectWait:ConnectWait,reply_wait=pass?RetryReplyWait:ReplyWait;
    const auto retry_peer=[&](const Peer& peer){if(!pass&&peer.stage!=Stage::Connect)remember({peer.ip,peer.nfs});};
    while(next<batch_total||std::any_of(peers.begin(),peers.end(),[](const auto& peer){return peer.fd>=0;})){
        if(progress->ShouldExit()){stopped=true;break;}
        const auto check=Check(progress);if(R_FAILED(check))return failure(check);
        const auto now=Clock::now();if(result.size()>=1024){stopped=true;break;}
        if(now>=network_check){
            u32 current{},current_mask{};const auto rc=Network(current,current_mask);
            if(R_FAILED(rc)||current!=address||current_mask!=mask){detail="The network connection changed during discovery. Connect to the intended LAN and scan again."_nx;return failure(R_FAILED(rc)?rc:nexus::Unavailable);}
            network_check=now+std::chrono::seconds(1);
        }
        for(auto& peer:peers){
            if(peer.fd>=0&&now>=peer.deadline){auto& stats=statistics[peer.nfs];++stats.timeouts;if(peer.stage!=Stage::Connect)++stats.reply_timeouts;retry_peer(peer);peer.Close();++finished;}
            if(peer.fd>=0||next>=batch_total)continue;
            const auto probe=pass?retries[next]:Probe{range.hosts[next/per_host],protocol=="NFS"||(protocol.empty()&&next%2==1)};
            peer.ip=probe.ip;peer.nfs=probe.nfs;++next;if(pass)++statistics[peer.nfs].retried;
            peer.fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(peer.fd<0){++socket_errors;++finished;continue;}
            // Discovery exchanges only small protocol messages. Do not give
            // 24 concurrent probes the large windows used by NAS downloads.
            const int buffer_size=16*1024;
            if(setsockopt(peer.fd,SOL_SOCKET,SO_SNDBUF,&buffer_size,sizeof(buffer_size))||
               setsockopt(peer.fd,SOL_SOCKET,SO_RCVBUF,&buffer_size,sizeof(buffer_size))){peer.Close();++socket_errors;++finished;continue;}
            const auto flags=fcntl(peer.fd,F_GETFL,0);
            if(flags<0||fcntl(peer.fd,F_SETFL,flags|O_NONBLOCK)<0){peer.Close();++socket_errors;++finished;continue;}
            ++opened;
            sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(peer.nfs?2049:445);endpoint.sin_addr.s_addr=htonl(peer.ip);
            const auto rc=connect(peer.fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint));
            if(rc<0&&errno!=EINPROGRESS&&errno!=EWOULDBLOCK&&errno!=EALREADY){++statistics[peer.nfs].connect_errors;peer.Close();++finished;continue;}
            peer.stage=rc==0?Stage::Send:Stage::Connect;peer.deadline=now+(rc==0?reply_wait:connect_wait);
        }
        std::array<pollfd,core::MaxPeers> fds{};
        for(size_t i=0;i<peers.size();++i){fds[i].fd=peers[i].fd;fds[i].events=peers[i].stage==Stage::Read?POLLIN:POLLOUT;}
        const auto polled=poll(fds.data(),fds.size(),50);
        if(polled<0){if(errno==EINTR)continue;detail="The network service could not complete discovery. Close other network tools and try again."_nx;return failure(MAKERESULT(Module_Libnx,LibnxError_IoError));}
        for(size_t i=0;i<peers.size();++i){auto& peer=peers[i];const auto events=fds[i].revents;if(peer.fd<0||!events)continue;
            const auto drop=[&]{peer.Close();++finished;};
            if(events&POLLNVAL){drop();continue;}
            if(peer.stage==Stage::Connect){int error{};socklen_t length=sizeof(error);
                if(getsockopt(peer.fd,SOL_SOCKET,SO_ERROR,&error,&length)||error){++statistics[peer.nfs].connect_errors;drop();continue;}
                peer.stage=Stage::Send;peer.deadline=Clock::now()+reply_wait;
            }
            if(peer.stage==Stage::Send){
                if(events&(POLLERR|POLLHUP)){++statistics[peer.nfs].closed;retry_peer(peer);drop();continue;}
#ifdef MSG_NOSIGNAL
                constexpr int send_flags=MSG_NOSIGNAL;
#else
                constexpr int send_flags=0;
#endif
                const std::span<const u8> request=peer.nfs?std::span<const u8>(nfs_request):std::span<const u8>(smb_request);
                const auto count=send(peer.fd,request.data()+peer.sent,request.size()-peer.sent,send_flags);
                if(count<0){if(Again())continue;++statistics[peer.nfs].closed;retry_peer(peer);drop();continue;}
                if(!count||static_cast<size_t>(count)>request.size()-peer.sent){++statistics[peer.nfs].closed;retry_peer(peer);drop();continue;}
                peer.sent+=count;if(peer.sent==request.size()){peer.stage=Stage::Read;peer.deadline=Clock::now()+reply_wait;}
            }
            if(peer.stage==Stage::Read){
                if(!(events&POLLIN)){if(events&(POLLERR|POLLHUP)){++statistics[peer.nfs].closed;retry_peer(peer);drop();}continue;}
                std::array<u8,4096> chunk{};const auto count=recv(peer.fd,chunk.data(),chunk.size(),0);
                if(count<0){if(Again())continue;++statistics[peer.nfs].closed;retry_peer(peer);drop();continue;}
                if(!count){++statistics[peer.nfs].closed;retry_peer(peer);drop();continue;}
                if(static_cast<size_t>(count)>chunk.size()||peer.reply.size()>core::MaxReply-static_cast<size_t>(count)){++statistics[peer.nfs].invalid;drop();continue;}
                peer.reply.insert(peer.reply.end(),chunk.begin(),chunk.begin()+count);
                const auto response=peer.nfs?core::NfsResponse(peer.reply,xid):core::Response(peer.reply);
                if(response==core::Reply::Valid){const auto ip=Address(peer.ip);if(!ip.empty()){
                    result.push_back({ip,ip,std::string(peer.nfs?"NFS":"SMB")+" at "+ip,peer.nfs?"NFS":"SMB"});
                    confirmed.push_back({peer.ip,peer.nfs});++statistics[peer.nfs].found;
                    if(!pass&&protocol.empty())remember({peer.ip,!peer.nfs});
                }drop();}
                else if(response==core::Reply::Invalid){++statistics[peer.nfs].invalid;drop();}
            }
        }
        progress->SetOverallTransfer(finished,total,std::string(pass?"Rechecking servers - "_nx:"Scanning network - "_nx)+std::to_string(result.size())+" services found"_nx);
    }
    }
    for(auto& peer:peers)peer.Close();
    if(!opened){detail="No network probe could be opened. Close other network tools and try again."_nx;return MAKERESULT(Module_Libnx,LibnxError_IoError);}
    if(!stopped){const auto final_check=Check(progress);if(R_FAILED(final_check))return failure(final_check);}
    const auto prefix=32-std::countr_zero(range.mask);
    // Counts only: never log addresses, credentials or server reply contents.
    for(size_t i=0;i<statistics.size();++i){const auto& stats=statistics[i];
        nexus::Log(i?"discovery-nfs-found":"discovery-smb-found",0,stats.found);
        nexus::Log(i?"discovery-nfs-timeouts":"discovery-smb-timeouts",0,stats.timeouts);
        nexus::Log(i?"discovery-nfs-reply-timeouts":"discovery-smb-reply-timeouts",0,stats.reply_timeouts);
        nexus::Log(i?"discovery-nfs-connect-errors":"discovery-smb-connect-errors",0,stats.connect_errors);
        nexus::Log(i?"discovery-nfs-invalid":"discovery-smb-invalid",0,stats.invalid);
        nexus::Log(i?"discovery-nfs-closed":"discovery-smb-closed",0,stats.closed);
        nexus::Log(i?"discovery-nfs-retried":"discovery-smb-retried",0,stats.retried);
    }
    nexus::Log("discovery-socket-errors",0,socket_errors);
    detail=std::to_string(statistics[0].found)+" SMB and "_nx+std::to_string(statistics[1].found)+" NFS service(s) found. Checked "_nx+std::to_string(finished)+" of "_nx+std::to_string(total)+" service probes on "_nx+Address(range.network)+"/"+std::to_string(prefix)+".\n\n";
    if(stopped)detail+="Scan stopped early; these are the servers found so far. Scan again to check the remaining addresses.\n\n"_nx;
    if(socket_errors)detail+="Some probes could not be opened. Retry after closing other network tools.\n\n"_nx;
    if(retry_limit)detail+="The reachable-server recheck limit was reached. A manual connection can check a missing server.\n\n"_nx;
    if(statistics[0].retried||statistics[1].retried)detail+="Reachable servers were rechecked with a longer wait for missing services.\n\n"_nx;
    detail+="The full connected IPv4 subnet is scanned. Other VLANs, sleeping or firewalled servers and nonstandard ports may need manual entry. Choose a server and shared folder to browse."_nx;
    return 0;
}
Result DiscoverSmbServers(ui::ProgressBox* p,std::vector<SmbServer>& servers,std::string& detail){return DiscoverNetworkServers(p,servers,detail,"SMB");}
}
