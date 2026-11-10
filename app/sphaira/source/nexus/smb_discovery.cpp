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
constexpr auto ConnectWait=std::chrono::milliseconds(900),ReplyWait=std::chrono::milliseconds(1800),ScanWait=std::chrono::milliseconds(15000);
enum class Stage {Connect,Send,Read};
struct Peer {
    int fd{-1};u32 ip{};Stage stage{};size_t sent{};Clock::time_point deadline{};core::Bytes reply;
    ~Peer(){Close();}
    void Close(){if(fd>=0)close(fd);fd=-1;reply.clear();sent=0;}
};
bool Again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
std::string Address(u32 ip){in_addr a{};a.s_addr=htonl(ip);char text[INET_ADDRSTRLEN]{};return inet_ntop(AF_INET,&a,text,sizeof(text))?text:std::string{};}
Result Network(u32& address,u32& mask){u32 gateway{},dns1{},dns2{};R_TRY(nifmGetCurrentIpConfigInfo(&address,&mask,&gateway,&dns1,&dns2));return address&&mask?0:nexus::Unavailable;}
Result Check(ui::ProgressBox* p){R_TRY(p->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);return 0;}
}
Result DiscoverSmbServers(ui::ProgressBox* progress,std::vector<SmbServer>& result,std::string& detail){
    result.clear();detail="SMB discovery did not finish. No shares or files were opened.";
    R_TRY(Check(progress));u32 address{},mask{};R_TRY(Network(address,mask));core::Range range;
    if(!core::Plan(ntohl(address),ntohl(mask),range)){
        detail="Automatic discovery needs a private IPv4 local network. Enter the server address manually for this connection.";return nexus::Unavailable;
    }
    std::array<u8,16> guid{};std::array<u8,32> salt{};randomGet(guid.data(),guid.size());randomGet(salt.data(),salt.size());
    guid[6]=(guid[6]&15)|0x40;guid[8]=(guid[8]&63)|0x80;
    const auto request=core::Negotiate(guid,salt);std::array<Peer,core::MaxPeers> peers;
    const auto start=Clock::now();auto network_check=start;size_t next{},finished{},socket_errors{},opened{};bool timed_out=false;
    progress->SetProgressNote("Finding SMB servers on this local network. No login or file access.");
    const auto failure=[&](Result rc){result.clear();return rc;};
    while(next<range.hosts.size()||std::any_of(peers.begin(),peers.end(),[](const auto& peer){return peer.fd>=0;})){
        const auto check=Check(progress);if(R_FAILED(check))return failure(check);
        const auto now=Clock::now();if(now-start>=ScanWait){timed_out=true;break;}
        if(now>=network_check){
            u32 current{},current_mask{};const auto rc=Network(current,current_mask);
            if(R_FAILED(rc)||current!=address||current_mask!=mask){detail="The network connection changed during discovery. Connect to the intended LAN and scan again.";return failure(R_FAILED(rc)?rc:nexus::Unavailable);}
            network_check=now+std::chrono::seconds(1);
        }
        for(auto& peer:peers){
            if(peer.fd>=0&&now>=peer.deadline){peer.Close();++finished;}
            if(peer.fd>=0||next>=range.hosts.size())continue;
            peer.ip=range.hosts[next++];peer.fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(peer.fd<0){++socket_errors;++finished;continue;}
            const auto flags=fcntl(peer.fd,F_GETFL,0);
            if(flags<0||fcntl(peer.fd,F_SETFL,flags|O_NONBLOCK)<0){peer.Close();++socket_errors;++finished;continue;}
            ++opened;
            sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(445);endpoint.sin_addr.s_addr=htonl(peer.ip);
            const auto rc=connect(peer.fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint));
            if(rc<0&&errno!=EINPROGRESS&&errno!=EWOULDBLOCK&&errno!=EALREADY){peer.Close();++finished;continue;}
            peer.stage=rc==0?Stage::Send:Stage::Connect;peer.deadline=now+(rc==0?ReplyWait:ConnectWait);
        }
        std::array<pollfd,core::MaxPeers> fds{};
        for(size_t i=0;i<peers.size();++i){fds[i].fd=peers[i].fd;fds[i].events=peers[i].stage==Stage::Read?POLLIN:POLLOUT;}
        const auto polled=poll(fds.data(),fds.size(),50);
        if(polled<0){if(errno==EINTR)continue;detail="The network service could not complete discovery. Close other network tools and try again.";return failure(MAKERESULT(Module_Libnx,LibnxError_IoError));}
        for(size_t i=0;i<peers.size();++i){auto& peer=peers[i];const auto events=fds[i].revents;if(peer.fd<0||!events)continue;
            const auto drop=[&]{peer.Close();++finished;};
            if(events&POLLNVAL){drop();continue;}
            if(peer.stage==Stage::Connect){int error{};socklen_t length=sizeof(error);
                if(getsockopt(peer.fd,SOL_SOCKET,SO_ERROR,&error,&length)||error){drop();continue;}
                peer.stage=Stage::Send;peer.deadline=Clock::now()+ReplyWait;
            }
            if(peer.stage==Stage::Send){
                if(events&(POLLERR|POLLHUP)){drop();continue;}
#ifdef MSG_NOSIGNAL
                constexpr int send_flags=MSG_NOSIGNAL;
#else
                constexpr int send_flags=0;
#endif
                const auto count=send(peer.fd,request.data()+peer.sent,request.size()-peer.sent,send_flags);
                if(count<0){if(Again())continue;drop();continue;}
                if(!count||static_cast<size_t>(count)>request.size()-peer.sent){drop();continue;}
                peer.sent+=count;if(peer.sent==request.size())peer.stage=Stage::Read;
            }
            if(peer.stage==Stage::Read){
                if(!(events&POLLIN)){if(events&(POLLERR|POLLHUP))drop();continue;}
                std::array<u8,4096> chunk{};const auto count=recv(peer.fd,chunk.data(),chunk.size(),0);
                if(count<0){if(Again())continue;drop();continue;}
                if(!count||static_cast<size_t>(count)>chunk.size()||peer.reply.size()>core::MaxReply-static_cast<size_t>(count)){drop();continue;}
                peer.reply.insert(peer.reply.end(),chunk.begin(),chunk.begin()+count);
                const auto response=core::Response(peer.reply);
                if(response==core::Reply::Valid){const auto ip=Address(peer.ip);if(!ip.empty())result.push_back({ip,ip,"SMB server at "+ip});drop();}
                else if(response==core::Reply::Invalid)drop();
            }
        }
        progress->SetOverallTransfer(finished,range.hosts.size(),"Discovering SMB servers");
    }
    for(auto& peer:peers)peer.Close();
    if(!opened){detail="No network probe could be opened. Close other network tools and try again.";return MAKERESULT(Module_Libnx,LibnxError_IoError);}
    const auto final_check=Check(progress);if(R_FAILED(final_check))return failure(final_check);
    const auto prefix=range.limited?24:32-std::countr_zero(range.mask);
    detail=std::to_string(result.size())+" SMB server(s) found. Checked "+std::to_string(finished)+" of "+std::to_string(range.hosts.size())+" addresses on "+Address(range.network)+"/"+std::to_string(prefix)+".\n\n";
    if(range.limited)detail+="This network is larger than one /24. Discovery checked only the Switch's local /24; enter other servers manually.\n\n";
    if(timed_out||socket_errors)detail+="Some addresses were not fully checked within the search limit. You can retry or enter a server manually.\n\n";
    detail+="Select a server, then enter its shared folder and login details. Discovery identifies SMB servers, not the shares you are allowed to open. Sleeping, blocked or slow servers may need manual entry.";
    return 0;
}
}
