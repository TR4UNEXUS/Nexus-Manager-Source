// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/network_shares.hpp"
#include "nexus/nx_Client.hpp"
#include "ui/progress_box.hpp"
#include "defines.hpp"
#include <poll.h>
#include <chrono>
#include <cerrno>
#ifdef ENABLE_DEVOPTAB_SMB2
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>
#endif
#ifdef ENABLE_DEVOPTAB_NFS
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include "libnfs-raw-mount.h"
#endif
namespace sphaira::nexusapp {
Result ListNetworkShares(ui::ProgressBox* p,const nexus::network::Connection& connection,const std::string& password,
                         std::vector<std::string>& shares,std::string& detail){
    shares.clear();detail="The server did not list its shared folders. Enter the share/export manually or check its permissions.";
    auto probe=connection;probe.name="Server";probe.share=probe.protocol=="SMB"?"IPC$":"/";
    R_UNLESS(nexus::network::Valid(probe),nexus::Invalid);R_TRY(p->ShouldExitResult());
    if(connection.protocol=="NFS"){
#ifdef ENABLE_DEVOPTAB_NFS
        // Mount export enumeration is a bounded read-only request. NFSv4-only
        // servers may intentionally omit mountd and require a manually entered
        // export/pseudo-root instead of treating an empty list as no server.
        auto* rpc=rpc_init_context();R_UNLESS(rpc,nexus::Unavailable);
        struct ExportReply {bool done{},valid{};std::vector<std::string> names;} reply;
        ON_SCOPE_EXIT(rpc_destroy_context(rpc));
        R_UNLESS(mount_getexports_async(rpc,connection.host.c_str(),[](rpc_context*,int status,void* data,void* user){
            auto& r=*static_cast<ExportReply*>(user);r.done=true;if(status||!data)return;r.valid=true;
            for(auto* entry=*static_cast<exports*>(data);entry;entry=entry->ex_next){
                if(r.names.size()>=1024||!entry->ex_dir){r.valid=false;return;}r.names.emplace_back(entry->ex_dir);
            }
        },&reply)==0,nexus::Unavailable);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
        while(!reply.done){R_TRY(p->ShouldExitResult());R_UNLESS(std::chrono::steady_clock::now()<deadline,nexus::Unavailable);
            pollfd fd{rpc_get_fd(rpc),static_cast<short>(rpc_which_events(rpc)),0};const auto rc=poll(&fd,1,50);
            if(rc<0){if(errno==EINTR)continue;return nexus::Unavailable;}R_UNLESS(rpc_service(rpc,fd.revents)>=0,nexus::Unavailable);
        }
        R_UNLESS(reply.valid,nexus::Unavailable);
        for(const auto& name:reply.names){auto c=probe;c.share=name;if(nexus::network::Valid(c))shares.push_back(name);}

#else
        return nexus::Unavailable;
#endif
    }else{
#ifdef ENABLE_DEVOPTAB_SMB2
        auto* smb=smb2_init_context();R_UNLESS(smb,nexus::Unavailable);
        smb2_set_security_mode(smb,SMB2_NEGOTIATE_SIGNING_ENABLED);smb2_set_timeout(smb,5);
        smb2_set_user(smb,connection.user.c_str());smb2_set_password(smb,password.c_str());smb2_set_domain(smb,connection.domain.c_str());
        struct Reply {bool done{};int status{};srvsvc_NetrShareEnum_rep* data{};} reply;
        ON_SCOPE_EXIT(smb2_destroy_context(smb));
        const auto wait=[&]()->Result{
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            while(!reply.done){R_TRY(p->ShouldExitResult());R_UNLESS(std::chrono::steady_clock::now()<deadline,nexus::Unavailable);
                pollfd fd{smb2_get_fd(smb),static_cast<short>(smb2_which_events(smb)),0};
                const auto rc=poll(&fd,1,50);if(rc<0){if(errno==EINTR)continue;return nexus::Unavailable;}
                R_UNLESS(smb2_service(smb,fd.revents)>=0,nexus::Unavailable);
            }
            return reply.status? nexus::Unavailable:0;
        };
        R_UNLESS(smb2_connect_share_async(smb,connection.host.c_str(),"IPC$",connection.user.c_str(),
            [](smb2_context*,int status,void*,void* data){auto& r=*static_cast<Reply*>(data);r.status=status;r.done=true;},&reply)==0,nexus::Unavailable);
        R_TRY(wait());reply={};
        R_UNLESS(smb2_share_enum_async(smb,SHARE_INFO_1,[](smb2_context*,int status,void* data,void* user){auto& r=*static_cast<Reply*>(user);r.status=status;r.data=status?nullptr:static_cast<srvsvc_NetrShareEnum_rep*>(data);r.done=true;},&reply)==0,nexus::Unavailable);
        const auto rc=wait();ON_SCOPE_EXIT(if(reply.data)smb2_free_data(smb,reply.data));R_TRY(rc);
        R_UNLESS(reply.data&&reply.data->status==0&&reply.data->ses.Level==1,nexus::Unavailable);
        const auto& list=reply.data->ses.ShareInfo.Level1;
        R_UNLESS(list.EntriesRead<=1024&&(!list.EntriesRead||(list.Buffer&&list.Buffer->max_count>=list.EntriesRead&&list.Buffer->share_info_1)),nexus::Invalid);
        for(u32 i=0;i<list.EntriesRead;++i){const auto& share=list.Buffer->share_info_1[i];if((share.type&3)!=SHARE_TYPE_DISKTREE||!share.netname.utf8)continue;
            auto c=probe;c.share=share.netname.utf8;if(nexus::network::Valid(c))shares.push_back(c.share);}
#else
        return nexus::Unavailable;
#endif
    }
    R_TRY(p->ShouldExitResult());std::sort(shares.begin(),shares.end());shares.erase(std::unique(shares.begin(),shares.end()),shares.end());
    detail=shares.empty()?"This server did not advertise an accessible folder. You can enter its share or export manually.":"Choose a shared folder. The server decides which folders this account can open.";
    return 0;
}
}
