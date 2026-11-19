#include "nexus/network_location_core.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <poll.h>
#include <vector>
#include <cerrno>
using u32=std::uint32_t;using Result=u32;
namespace nexus {constexpr Result Invalid=99,Unavailable=100;}
#define R_TRY(x) {const auto rc_=(x);if(rc_)return rc_;}
#define R_UNLESS(x,r) {if(!(x))return r;}
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
template<class F>struct Exit{F f;~Exit(){f();}};
#define ON_SCOPE_EXIT(...) auto JOIN(exit_,__LINE__)=Exit{[&](){__VA_ARGS__;}}
namespace fixture {inline int elapsed{},cancel_at{-1},created{},closed{},freed{};inline bool oom{},stall{},connect_fail{},enumerate_fail{},oversized{},bad_type{};inline std::string user,password,domain;}
struct FakeClock{static auto now(){return std::chrono::steady_clock::time_point(std::chrono::milliseconds(fixture::elapsed));}};
namespace sphaira::ui {struct ProgressBox{Result ShouldExitResult(){return fixture::cancel_at>=0&&fixture::elapsed>=fixture::cancel_at?101:0;}};}
struct Context{std::function<void(bool)> callback;};using smb2_context=Context;using rpc_context=Context;
enum SHARE_INFO_enum{SHARE_INFO_1=1};constexpr int SMB2_NEGOTIATE_SIGNING_ENABLED=1,SHARE_TYPE_DISKTREE=0;
struct Utf{const char* utf8;};struct Share{Utf netname;u32 type;};struct Buffer{u32 max_count;Share* share_info_1;};
struct Level1{u32 EntriesRead;struct Buffer* Buffer;};
struct srvsvc_NetrShareEnum_rep{u32 status;struct {u32 Level;struct {struct Level1 Level1;} ShareInfo;} ses;};
struct exportnode{char* ex_dir;exportnode* ex_next;};using exports=exportnode*;
Context* New(){if(fixture::oom)return nullptr;++fixture::created;return new Context;}
void Destroy(Context* c){if(c->callback){auto cb=std::move(c->callback);cb(true);}++fixture::closed;delete c;}
Context* smb2_init_context(){return New();}void smb2_destroy_context(Context* c){Destroy(c);}
void smb2_set_security_mode(Context*,int){}void smb2_set_timeout(Context*,int){}
void smb2_set_user(Context*,const char* s){fixture::user=s;}void smb2_set_password(Context*,const char* s){fixture::password=s;}void smb2_set_domain(Context*,const char* s){fixture::domain=s;}
int smb2_get_fd(Context*){return 1;}int smb2_which_events(Context*){return POLLIN;}
int Service(Context* c){if(!fixture::stall&&c->callback){auto cb=std::move(c->callback);cb(false);}return 0;}
int smb2_service(Context* c,int){return Service(c);}
using SmbCallback=void(*)(Context*,int,void*,void*);
int smb2_connect_share_async(Context* c,const char*,const char* share,const char*,SmbCallback cb,void* data){assert(std::string(share)=="IPC$");c->callback=[=](bool cancel){cb(c,cancel||fixture::connect_fail?-1:0,nullptr,data);};return 0;}
int smb2_share_enum_async(Context* c,SHARE_INFO_enum,SmbCallback cb,void* data){c->callback=[=](bool cancel){
    if(cancel||fixture::enumerate_fail){cb(c,-1,nullptr,data);return;}
    static Share shares[]={{{"Games"},0},{{"IPC$"},3},{{"Print queue"},1},{{"Games"},0},{{"Backups"},0}};static Buffer buffer{5,shares};static Level1 list{5,&buffer};
    auto* reply=new srvsvc_NetrShareEnum_rep{0,{fixture::bad_type?0u:1u,{list}}};if(fixture::oversized)reply->ses.ShareInfo.Level1.EntriesRead=2000;cb(c,0,reply,data);
};return 0;}
void smb2_free_data(Context*,void* data){++fixture::freed;delete static_cast<srvsvc_NetrShareEnum_rep*>(data);}
Context* rpc_init_context(){return New();}void rpc_destroy_context(Context* c){Destroy(c);}int rpc_get_fd(Context*){return 2;}int rpc_which_events(Context*){return POLLIN;}int rpc_service(Context* c,int){return Service(c);}
using RpcCallback=void(*)(Context*,int,void*,void*);
int mount_getexports_async(Context* c,const char*,RpcCallback cb,void* data){c->callback=[=](bool cancel){
    if(cancel||fixture::enumerate_fail){cb(c,-1,nullptr,data);return;}
    char a[]="/exports/games",b[]="/exports/backups";exportnode tail{b,nullptr},first{a,&tail};exports head=&first;cb(c,0,&head,data);
};return 0;}
int Poll(pollfd* fd,unsigned long count,int ms){assert(count==1&&ms==50);fixture::elapsed+=ms;fd->revents=POLLIN;return 1;}
#define poll Poll
namespace sphaira::nexusapp {
// PRODUCTION
}
#undef poll
void Reset(){using namespace fixture;assert(created==closed);elapsed=created=closed=freed=0;cancel_at=-1;oom=stall=connect_fail=enumerate_fail=oversized=bad_type=false;user=password=domain="";}
int main(){using namespace fixture;using sphaira::nexusapp::ListNetworkShares;sphaira::ui::ProgressBox p;std::vector<std::string> shares;std::string detail;
    nexus::network::Connection c{"NAS","SMB","192.168.1.2","","user","DOMAIN"};
    Reset();assert(!ListNetworkShares(&p,c,"private-password",shares,detail));assert((shares==std::vector<std::string>{"Backups","Games"}));assert(closed==1&&freed==1&&password=="private-password"&&domain=="DOMAIN"&&detail.find(password)==std::string::npos);
    for(unsigned fault=0;fault<7;++fault){Reset();if(fault==0)oom=true;if(fault==1)connect_fail=true;if(fault==2)enumerate_fail=true;if(fault==3)oversized=true;if(fault==4)bad_type=true;if(fault==5)stall=true;if(fault==6){stall=true;cancel_at=100;}
        assert(ListNetworkShares(&p,c,"secret",shares,detail));assert(created==closed&&elapsed<=12000);}
    c.protocol="NFS";Reset();assert(!ListNetworkShares(&p,c,"",shares,detail));assert((shares==std::vector<std::string>{"/exports/backups","/exports/games"})&&created==closed);
    for(unsigned fault=0;fault<4;++fault){Reset();if(fault==0)oom=true;if(fault==1)enumerate_fail=true;if(fault==2)stall=true;if(fault==3){stall=true;cancel_at=100;}
        assert(ListNetworkShares(&p,c,"",shares,detail));assert(created==closed&&elapsed<=12000);}
    std::cout<<"Production SMB share/NFS export enumeration: authentication, folder filtering, malformed replies, timeout, OOM, cancellation and callback lifetime passed\n";
}
