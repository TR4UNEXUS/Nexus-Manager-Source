// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/devoptab_common.hpp"
#include "utils/devoptab.hpp"
#include "nexus/network_location_core.hpp"
#include "nexus/network_session_passwords.hpp"
#include "nexus/network_socket_config.hpp"
#include "nexus/network_source.hpp"
#include "nexus/network_install.hpp"
#include "nexus/smb_discovery.hpp"
#include "nexus/network_shares.hpp"
#include "nexus/package.hpp"
#include "nexus/file_manager.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/update.hpp"
#include "nexus/transfer.hpp"
#include "nexus/nx_Client.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/network_browser.hpp"
#include "utils/utils.hpp"
#include "app.hpp"
#include <minIni.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <set>

namespace sphaira::devoptab {
namespace {
using namespace ui;
using namespace nexus::files;
using Connection=nexus::network::Connection;
using BrowserModel=nexus::network::browser::Model;
using BrowserState=std::shared_ptr<BrowserModel>;
using BrowserRow=nexus::network::browser::Row;
using BrowserKind=nexus::network::browser::Kind;
using SelectedNetworkFile=nexus::network::browser::SelectedFile;
constexpr Result IoError=MAKERESULT(Module_Libnx,LibnxError_IoError);
constexpr const char* ConfigPath="sdmc:/config/nexusmanager/network.ini";
nexus::network::SessionPasswords session_passwords;
struct Session {
    fs::FsPath mount;std::string name,protocol;bool sockets{},network{},installing{};
    Result Open(std::string* detail=nullptr){
        if(detail)detail->clear();
        const auto defaults=*socketGetDefaultInitConfig();
        const auto download=nexus::network::DownloadSocketConfig(defaults);
        auto socket_result=socketInitialize(&download);
        const bool fallback=socket_result==MAKERESULT(Module_Libnx,LibnxError_OutOfMemory)||
            socket_result==MAKERESULT(Module_Kernel,KernelError_OutOfMemory);
        if(fallback){
            // Failed libnx initialization cleans up its own allocation. Retry
            // only memory pressure, never an already-owned socket stack.
            nexus::Log("network-sockets-memory-fallback",socket_result);
            socket_result=socketInitialize(&defaults);
        }
        nexus::Log("network-sockets",socket_result);
        if(socket_result){
            if(detail)*detail=socket_result==MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized)
                ? "Another network connection is still open. Close the other network browser or transfer, then try again. If it remains open, close and reopen Nexus Manager."
                : "The Switch network sockets could not be opened. Close and reopen Nexus Manager, check Wi-Fi, then try again.";
            return socket_result;
        }
        sockets=true;
        nexus::Log("network-socket-pool",0,nexus::network::SocketTransferMemorySize(fallback?defaults:download));
        const auto network_result=nifmInitialize(NifmServiceType_User);
        nexus::Log("network-nifm",network_result);
        if(network_result){if(detail)*detail="The Switch network service could not be opened. Close and reopen Nexus Manager, check Wi-Fi, then try again.";return network_result;}
        network=true;
        u32 address{};const auto address_result=nifmGetCurrentIpAddress(&address);
        const auto result=address_result?address_result:address?Result{}:nexus::Unavailable;
        // Record success/failure only; do not write the address or login data.
        nexus::Log("network-ip",result);
        if(result&&detail)*detail="The Switch does not have a usable network address. Connect it to the same local network as the NAS, then try again.";
        return result;
    }
    ~Session(){if(!mount.empty())UmountNeworkDevice(mount);if(network)nifmExit();if(sockets)socketExit();}
};
using SessionPtr=std::shared_ptr<Session>;
void Message(const std::string& text){App::Push<OptionBox>(text,"OK",[](auto){});}
std::vector<Connection> Load(){
    std::vector<Connection> entries;
    ini_browse([](const char* section,const char* key,const char* value,void* arg)->int{
        auto& rows=*static_cast<std::vector<Connection>*>(arg);if(!section||!key||!value)return 1;
        if(rows.empty()||rows.back().name!=section){if(rows.size()>=64)return 0;rows.emplace_back();rows.back().name=section;}
        auto& c=rows.back();
        if(!strcmp(key,"protocol"))c.protocol=value;else if(!strcmp(key,"host"))c.host=value;
        else if(!strcmp(key,"share"))c.share=value;else if(!strcmp(key,"user"))c.user=value;else if(!strcmp(key,"domain"))c.domain=value;
        else if(!strcmp(key,"remember_password"))c.remember_password=strcmp(value,"0")!=0;
        return 1;
    },&entries,ConfigPath);
    return entries;
}
bool ReadSetting(const Connection& c,const char* key,std::string& value){
    value.clear();if(!ini_haskey(c.name.c_str(),key,ConfigPath))return false;
    std::array<char,600> text{};
    const auto size=ini_gets(c.name.c_str(),key,"",text.data(),static_cast<int>(text.size()),ConfigPath);
    if(size<0||size>=static_cast<int>(text.size())-1)return false;
    value.assign(text.data(),size);return true;
}
Result WriteSetting(const Connection& c,const char* key,const std::string& value){
    R_UNLESS(ini_puts(c.name.c_str(),key,value.c_str(),ConfigPath),IoError);
    std::string readback;R_UNLESS(ReadSetting(c,key,readback)&&readback==value,IoError);return 0;
}
constexpr std::array<const char*,10> PasswordFields{{"password_saved","password_hex_0","password_hex_1",
    "password_protocol","password_host","password_share","password_user","password_domain","pass","password"}};
Result ClearSavedPassword(const Connection& c){
    // Remove the commit marker first. A partial write/delete must never activate
    // old or incomplete credentials under an edited server/account.
    for(const auto* key:PasswordFields){
        R_UNLESS(ini_puts(c.name.c_str(),key,nullptr,ConfigPath),IoError);
        R_UNLESS(!ini_haskey(c.name.c_str(),key,ConfigPath),IoError);
    }
    return 0;
}
bool SavedEndpoint(const Connection& c){
    if(!nexus::network::Valid(c))return false;
    const auto rows=Load();return std::any_of(rows.begin(),rows.end(),[&](const auto& saved){
        return saved.name==c.name&&saved.remember_password==c.remember_password&&nexus::network::SameEndpoint(saved,c);
    });
}
bool ReadSavedPassword(const Connection& c,std::string& password){
    password.clear();if(c.protocol!="SMB"||!c.remember_password||!SavedEndpoint(c))return false;
    std::string text;if(!ReadSetting(c,"password_saved",text)||text!="1")return false;
    const std::array<std::pair<const char*,std::string>,5> binding{{{"password_protocol",c.protocol},{"password_host",c.host},{"password_share",c.share},{"password_user",c.user},{"password_domain",c.domain}}};
    for(const auto& [key,value]:binding)if(!ReadSetting(c,key,text)||text!=value)return false;
    std::string hex,tail;if(!ReadSetting(c,"password_hex_0",hex)||!ReadSetting(c,"password_hex_1",tail)||hex.size()>512||tail.size()>512)return false;
    hex+=tail;if(hex.size()%2)return false;
    auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    std::string decoded;decoded.reserve(hex.size()/2);
    for(size_t i=0;i<hex.size();i+=2){const auto hi=digit(hex[i]),lo=digit(hex[i+1]);if(hi<0||lo<0||!(hi||lo))return false;decoded+=static_cast<char>((hi<<4)|lo);}
    password=std::move(decoded);return true;
}
Result SavePassword(const Connection& c,const std::string& password){
    R_UNLESS(c.protocol=="SMB"&&c.remember_password&&SavedEndpoint(c)&&password.size()<=512&&password.find('\0')==password.npos,nexus::Invalid);
    std::string existing;if(ReadSavedPassword(c,existing)&&existing==password)return 0;
    R_TRY(ClearSavedPassword(c));
    // Hex preserves whitespace, quotes and INI comment characters exactly. It
    // is encoding, NOT encryption; the settings UI says this is stored on SD.
    std::string hex;hex.reserve(password.size()*2);constexpr char digits[]="0123456789abcdef";
    for(unsigned char byte:password){hex+=digits[byte>>4];hex+=digits[byte&15];}
    const std::array<std::pair<const char*,std::string>,7> fields{{{"password_protocol",c.protocol},{"password_host",c.host},{"password_share",c.share},{"password_user",c.user},{"password_domain",c.domain},{"password_hex_0",hex.substr(0,512)},{"password_hex_1",hex.size()>512?hex.substr(512):std::string{}}}};
    for(const auto& [key,value]:fields)R_TRY(WriteSetting(c,key,value));
    R_TRY(WriteSetting(c,"password_saved","1"));
    std::string readback;R_UNLESS(ReadSavedPassword(c,readback)&&readback==password,IoError);return 0;
}
bool KnownPassword(const Connection& c,std::string& password){
    if(c.protocol!="SMB"){password.clear();return false;}
    if(session_passwords.Find(c,password))return true;
    if(!ReadSavedPassword(c,password))return false;
    session_passwords.Remember(c,password);return true;
}
Result Save(const Connection& c,const std::string* password=nullptr){
    // minIni trims section names. Refuse a name that cannot reopen exactly;
    // the connection form removes accidental surrounding spaces first.
    R_UNLESS(nexus::network::Valid(c)&&c.name.front()!=' '&&c.name.back()!=' ',nexus::Invalid);
    R_UNLESS(!password||(password->size()<=512&&password->find('\0')==password->npos),nexus::Invalid);
    R_TRY(fs::FsNativeSd().CreateDirectoryRecursively("/config/nexusmanager"));
    std::string previous;if(!password&&ReadSavedPassword(c,previous))password=&previous;
    R_TRY(ClearSavedPassword(c));
    const std::array<std::pair<const char*,std::string>,6> values{{{"protocol",c.protocol},{"host",c.host},{"share",c.share},{"user",c.user},{"domain",c.domain},{"remember_password",c.remember_password?"1":"0"}}};
    // minIni misses some write/close/rename errors; verify every persisted value.
    for(const auto& [key,value]:values)R_TRY(WriteSetting(c,key,value));
    if(c.protocol=="SMB"&&c.remember_password&&password)R_TRY(SavePassword(c,*password));
    return 0;
}
Result SaveConnectedLocation(Connection c,const std::string& password){
    const auto rows=Load();
    // Reuse the saved endpoint's name; a new share on the same NAS gets its
    // own name instead of silently replacing another share or its login.
    const auto same=std::find_if(rows.begin(),rows.end(),[&](const auto& old){return nexus::network::SameEndpoint(old,c);});
    if(same!=rows.end())c.name=same->name;
    else {
        R_UNLESS(rows.size()<64,nexus::Invalid);
        const auto base=c.name;
        for(unsigned suffix=2;std::any_of(rows.begin(),rows.end(),[&](const auto& old){return old.name==c.name;});++suffix){
            R_UNLESS(suffix<=65,nexus::Invalid);const auto tail=" ("+std::to_string(suffix)+")";
            c.name=base.substr(0,64-tail.size())+tail;
        }
    }
    R_TRY(Save(c,&password));session_passwords.Remember(c,password);return 0;
}
void BrowseServerShares(Connection,const std::string&,bool);
Result Forget(const Connection& c){
    R_TRY(ClearSavedPassword(c));
    R_UNLESS(ini_puts(c.name.c_str(),nullptr,nullptr,ConfigPath),IoError);
    const auto rows=Load();R_UNLESS(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.name==c.name;}),IoError);
    session_passwords.Forget(c);return 0;
}
struct Hash {Sha256Context ctx{};Hash(){sha256ContextCreate(&ctx);}void Update(const void* p,size_t n){sha256ContextUpdate(&ctx,p,n);}Digest Final(){Digest d{};sha256ContextGetHash(&ctx,d.data());return d;}};
// The transfer engine sees two fixed volumes: false=the pinned network mount,
// true=the pinned Nexus USB generation. It never opens or falls back to SD.
class CopyIO {
public:
    using Result=::Result;using Hash=::sphaira::devoptab::Hash;
    static constexpr Result Invalid=nexus::Invalid,Conflict=MAKERESULT(2,2);
private:
    SessionPtr session;ProgressBox* progress;u64 generation{};nexus::Client client;
    nexus::network::Source source;std::string stage,final,progress_path,progress_action;
    bool Owned(const std::string& path)const{return !stage.empty()&&Canonical(path)&&Within(path,stage);}
    Result Mutation(){R_TRY(Check());return nexus::CheckUsbMutationAllowed();}
public:
    CopyIO(SessionPtr s,ProgressBox* p,u64 g=0,std::string destination={}):session(std::move(s)),progress(p),generation(g),source(session->mount.toString(),[this]{return R_SUCCEEDED(Check());}),final(std::move(destination)){}
    ~CopyIO(){EndRead();}
    Result Check(){if(progress)R_TRY(progress->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        if(generation){R_TRY(client.Check(generation));NexusVsdStatus status{};R_TRY(client.VirtualSdStatus(status));R_UNLESS(!(status.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}return 0;}
    size_t BufferSize()const{return std::min<size_t>(256*1024,client.TransferLimit());}
    void Progress(const char* action,const std::string& path,u64 done,u64 total){if(!progress)return;if(progress_path!=path||progress_action!=action){progress->NewTransfer(path);progress_path=path;progress_action=action;}
        progress->SetTitle(path).SetActionName(action).UpdateTransfer(done,total).SetProgressNote("Keep the network and USB drive connected. The network source is kept.");}
    void EndRead(){source.EndRead();}
    Result Stat(bool usb,const std::string& path,Entry& e,bool& exists){e={};exists=false;R_UNLESS(Canonical(path),Invalid);R_TRY(Check());
        if(usb){nexus::Info info{};auto rc=client.Stat(generation,path,info);if(rc==MAKERESULT(140,205))return 0;R_TRY(rc);R_UNLESS(info.directory<=1,Invalid);e.directory=info.directory;e.size=e.directory?0:info.size;}
        else {const auto error=source.Stat(path,e,exists);R_TRY(Check());if(error==ENOTSUP||error==EINVAL)return Invalid;R_UNLESS(!error,IoError);return 0;}
        e.path=path;exists=true;return 0;}
    Result List(bool usb,const std::string& path,std::vector<std::string>& names){names.clear();R_UNLESS(Canonical(path),Invalid);R_TRY(Check());
        if(usb){std::vector<nexus::Entry> entries;R_TRY(client.List(generation,path,entries));R_UNLESS(entries.size()<=MaxEntries,Invalid);for(const auto& e:entries){R_UNLESS(memchr(e.name,0,sizeof(e.name)),Invalid);names.emplace_back(e.name);}}
        else {const auto error=source.List(path,names);R_TRY(Check());R_UNLESS(!error,IoError);}
        return 0;}
    Result Read(bool usb,const std::string& path,u64 offset,void* data,size_t count,u64& actual){actual=0;R_UNLESS(Canonical(path)&&offset<=MaxBytes&&count<=MaxBytes-offset,Invalid);R_TRY(Check());
        if(usb)return client.Read(generation,path,offset,data,count,actual);
        const auto error=source.Read(path,offset,data,count,actual);R_TRY(Check());R_UNLESS(!error,IoError);return 0;}
    Result Preflight(u64 bytes){R_TRY(Check());nexus::Space space{};R_TRY(client.GetSpace(generation,space));R_UNLESS(bytes<=space.free&&space.free-bytes>=32*1024*1024,MAKERESULT(2,30));return 0;}
    Result NewStage(bool directory,u64 size,std::string& out){R_UNLESS(Safe(final),Invalid);std::array<u8,16> nonce{};randomGet(nonce.data(),nonce.size());stage=Child(Parent(final),std::string(StagePrefix)+nexusapp::HexId(nonce.data(),nonce.size()));
        Entry e;bool exists{};R_TRY(Stat(true,stage,e,exists));R_UNLESS(!exists,Conflict);out=stage;return directory?Directory(true,stage):Create(true,stage,size);}
    Result Directory(bool usb,const std::string& path){R_UNLESS(usb&&Owned(path),Invalid);R_TRY(Mutation());return client.CreateDirectory(generation,path);}
    Result Create(bool usb,const std::string& path,u64 size){R_UNLESS(usb&&Owned(path)&&size<=MaxBytes,Invalid);R_TRY(Mutation());return client.CreateFile(generation,path,size);}
    Result Write(bool usb,const std::string& path,u64 off,const void* data,size_t size){R_UNLESS(usb&&Owned(path),Invalid);R_TRY(Mutation());return client.Write(generation,path,off,data,size);}
    Result Flush(bool usb){R_UNLESS(usb,Invalid);R_TRY(Mutation());return client.Flush(generation);}
    Result Publish(const std::string& from,const std::string& to,bool){R_UNLESS(from==stage&&to==final&&Safe(to),Invalid);R_TRY(Mutation());return client.RenameNew(generation,from,to);}
    Result Remove(bool,const std::string&,u64,bool){return Invalid;} // copy-only
};
void Browse(SessionPtr,const std::string&,BrowserState state={});
void Destination(SessionPtr,const std::string&,const std::string&,BrowserState state={});
void Locations(const std::string&,bool);
void DiscoverSmb(bool);
void PrepareInstall(SessionPtr session,const Entry& item,nexus::Target target){
    auto generation=std::make_shared<u64>();
    const std::string destination=target==nexus::Target::Sd?"SD Card":"USB Drive";
    App::Push<ProgressBox>(0,"Check install destination",destination,[=](ProgressBox* p)->Result{
        R_TRY(p->ShouldExitResult());R_TRY(nexus::CheckUsbMutationAllowed());
        CopyIO io(session,p);Entry current;bool exists{};R_TRY(io.Stat(false,item.path,current,exists));
        R_UNLESS(exists&&!current.directory&&current.size==item.size&&nexus::package::Supported(item.path),nexus::Invalid);
        nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
        if(target==nexus::Target::Usb){nexus::Client usb;nexus::Status status{};R_TRY(usb.Query(status));R_UNLESS(status.Available(),nexus::Unavailable);*generation=status.generation;}
        R_TRY(nexus::QueryDestination(target,space));
        // NSP size is not installed size. Yati checks the real content sizes.
        R_UNLESS(space.free>=32*1024*1024,MAKERESULT(2,30));return p->ShouldExitResult();
    },[=](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not prepare this install. Check the NAS file and selected drive, finish other transfers and restore any pending USB library recovery before trying again. Nothing was installed.");return;}
        App::Push<OptionBox>("Install this file to "+destination+"?\n\n"+Leaf(item.path)+"\nFrom: "+session->protocol+" / "+session->name+"\n\nThe game file will be read directly from the NAS. No package copy is needed on SD or USB. The original file stays on the NAS.\n\nKeep the network and destination connected until finished.","Cancel","Install",0,[=](auto choice){
            if(!choice||*choice!=1)return;
            auto diagnostic=std::make_shared<nexus::install::Diagnostic>();
            App::Push<ProgressBox>(0,"Install from "+session->protocol,Leaf(item.path)+" > "+destination,[=](ProgressBox* p){
                return nexusapp::InstallNetworkPackage(p,session->mount.toString(),item.path,item.size,target,*generation,*diagnostic);
            },[=](Result result){
                if(R_FAILED(result)){App::PushErrorBox(result,"Install from "+session->protocol+" to "+destination+" stopped.\n\n"+diagnostic->text);return;}
                Message("NAS install finished\n\nDestination: "+destination+"\nFile: "+Leaf(item.path)+"\n\n"+diagnostic->completion);
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void ChooseInstallDestination(SessionPtr session,const Entry& item){
    auto menu=std::make_unique<Sidebar>("Choose install destination",Leaf(item.path),Sidebar::Side::RIGHT);
    menu->Add<SidebarEntryCallback>("SD Card",[=]{PrepareInstall(session,item,nexus::Target::Sd);},"Install the game directly from your NAS to the SD card.");
    menu->Add<SidebarEntryCallback>("USB Drive",[=]{PrepareInstall(session,item,nexus::Target::Usb);},"Install the game directly from your NAS to the connected USB drive.");
    App::Push(std::move(menu));
}
void PickPackage(SessionPtr session,const Entry& item){
    auto menu=std::make_unique<Sidebar>("Picked file",Leaf(item.path),Sidebar::Side::RIGHT);
    menu->Add<SidebarEntryCallback>("Install picked file",[=]{ChooseInstallDestination(session,item);},"Next choose SD Card or USB Drive. The original file stays on the NAS.");
    App::Push(std::move(menu));
}
struct NetworkQueueRecord {
    enum class State {Waiting,Finished,Skipped,Failed,Cancelled};
    SelectedNetworkFile file;
    State state{State::Waiting};Result result{};
    size_t installed_records{},skipped_records{};bool attempted{};
    std::string detail{};
};
using NetworkQueueReport=std::vector<NetworkQueueRecord>;
Result CheckInstallQueue(SessionPtr session,ProgressBox* progress,nexus::Target target,u64 generation){
    R_UNLESS(progress&&session&&!session->mount.empty()&&(target==nexus::Target::Sd||target==nexus::Target::Usb),nexus::Invalid);
    R_TRY(progress->ShouldExitResult());
    R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
    // A queue is bound to this one mounted session. Do not reconnect, fall
    // back to another share, or pick up a replacement destination drive.
    location::StdioEntries devices;R_TRY(GetNetworkDevices(devices));
    const auto device=std::find_if(devices.begin(),devices.end(),[&](const auto& entry){return entry.mount==session->mount.toString();});
    R_UNLESS(device!=devices.end()&&(device->flags&location::FsEntryFlag::FsEntryFlag_ReadOnly)&&
        !(device->flags&location::FsEntryFlag::FsEntryFlag_NoStatFile),nexus::Unavailable);
    R_TRY(nexus::CheckUsbMutationAllowed());nexus::Space space{};R_TRY(nexus::QueryPhysicalSd(space));
    if(target==nexus::Target::Usb){R_UNLESS(generation,nexus::Invalid);nexus::Client usb;R_TRY(usb.Check(generation));}
    else R_UNLESS(!generation,nexus::Invalid);
    return 0;
}
Result RunInstallQueue(SessionPtr session,const std::vector<SelectedNetworkFile>& files,nexus::Target target,u64 generation,ProgressBox* progress,NetworkQueueReport& report){
    report.clear();for(const auto& file:files)report.push_back({file});
    R_UNLESS(session&&progress&&!files.empty(),nexus::Invalid);
    if(files.size()>nexus::network::browser::MaxSelection){report.front().state=NetworkQueueRecord::State::Failed;report.front().result=nexus::Invalid;report.front().detail="Select up to 64 files for one queue.";return nexus::Invalid;}
    std::set<std::string> paths;
    for(auto& row:report)if(!Canonical(row.file.path)||!nexus::package::Supported(row.file.path)||row.file.size<16||row.file.size>MaxBytes||!paths.insert(row.file.path).second){
        row.state=NetworkQueueRecord::State::Failed;row.result=nexus::Invalid;row.detail="This selection is no longer valid. Browse the NAS and select the file again.";return row.result;
    }
    for(size_t i=0;i<report.size();++i){
        auto& row=report[i];
        progress->SetImage(0).SetTitle(Leaf(row.file.path)).SetProgressNote({});
        progress->SetActionName("Install "+std::to_string(i+1)+" / "+std::to_string(report.size())+" from "+session->protocol);
        auto result=CheckInstallQueue(session,progress,target,generation);
        if(R_SUCCEEDED(result)){
            nexus::install::Diagnostic diagnostic;nexusapp::NetworkInstallOutcome outcome;
            result=nexusapp::InstallNetworkPackage(progress,session->mount.toString(),row.file.path,row.file.size,target,generation,diagnostic,&outcome);
            row.attempted=outcome.attempted;row.installed_records=outcome.installed_records;row.skipped_records=outcome.skipped_records;
            row.detail=R_SUCCEEDED(result)?diagnostic.completion:diagnostic.text;
        }else row.detail="The queue stopped before this file started. Check the network connection and the selected destination before retrying.";
        row.result=result;
        if(R_FAILED(result)){
            row.state=progress->ShouldExit()?NetworkQueueRecord::State::Cancelled:NetworkQueueRecord::State::Failed;
            if(row.state==NetworkQueueRecord::State::Cancelled&&!row.attempted)row.detail="Cancelled before this file started.";
            return result;
        }
        row.state=row.installed_records?NetworkQueueRecord::State::Finished:NetworkQueueRecord::State::Skipped;
        // Recheck the same mount and generation before the next file. A user
        // cancellation arriving at this boundary must stop the queue too.
    }
    return 0;
}
std::string InstallQueueSummary(const NetworkQueueReport& report,const std::string& destination){
    size_t installed{},skipped{},failed{},cancelled{},waiting{};
    for(const auto& row:report)switch(row.state){
        case NetworkQueueRecord::State::Finished:++installed;break;
        case NetworkQueueRecord::State::Skipped:++skipped;break;
        case NetworkQueueRecord::State::Failed:++failed;break;
        case NetworkQueueRecord::State::Cancelled:++cancelled;break;
        case NetworkQueueRecord::State::Waiting:++waiting;break;
    }
    std::string text="NAS install queue "+std::string(failed||cancelled||waiting?"stopped":"finished")+"\n\nDestination: "+destination+
        "\nInstalled: "+std::to_string(installed)+" files\nSkipped: "+std::to_string(skipped)+" files\nFailed: "+std::to_string(failed)+
        " files\nCancelled: "+std::to_string(cancelled)+" files\nNot started: "+std::to_string(waiting)+" files\n\n";
    for(const auto& row:report){
        const char* status="Not started";
        switch(row.state){case NetworkQueueRecord::State::Finished:status="Installed";break;case NetworkQueueRecord::State::Skipped:status="Skipped";break;
            case NetworkQueueRecord::State::Failed:status="Failed";break;case NetworkQueueRecord::State::Cancelled:status="Cancelled";break;case NetworkQueueRecord::State::Waiting:break;}
        text+=std::string(status)+": "+row.file.path+"\n";
        if(!row.detail.empty())text+=row.detail+"\n";
        if((row.state==NetworkQueueRecord::State::Failed||row.state==NetworkQueueRecord::State::Cancelled)&&(row.installed_records||row.skipped_records))
            text+="Completed entries before stopping: "+std::to_string(row.installed_records)+" installed, "+std::to_string(row.skipped_records)+" skipped.\n";
        if(row.result){char code[32]{};std::snprintf(code,sizeof(code),"Error: 0x%08X\n",unsigned(row.result));text+=code;}
        text+='\n';
    }
    text+="The original files stay on the NAS. Files that failed, were cancelled or were not started remain selected for review and retry.";
    return text;
}
void PrepareInstallQueue(SessionPtr session,std::vector<SelectedNetworkFile> files,nexus::Target target,std::weak_ptr<BrowserModel> browser){
    if(files.empty())return;
    auto generation=std::make_shared<u64>();const std::string destination=target==nexus::Target::Sd?"SD Card":"USB Drive";
    App::Push<ProgressBox>(0,"Check selected files",std::to_string(files.size())+" files > "+destination,[=](ProgressBox* progress)->Result{
        R_UNLESS(files.size()<=nexus::network::browser::MaxSelection,nexus::Invalid);
        if(target==nexus::Target::Usb){nexus::Client usb;nexus::Status status{};R_TRY(usb.Query(status));R_UNLESS(status.Available(),nexus::Unavailable);*generation=status.generation;}
        R_TRY(CheckInstallQueue(session,progress,target,*generation));
        CopyIO io(session,progress);std::set<std::string> paths;
        for(const auto& file:files){
            R_TRY(progress->ShouldExitResult());R_UNLESS(Canonical(file.path)&&nexus::package::Supported(file.path)&&file.size>=16&&file.size<=MaxBytes&&paths.insert(file.path).second,nexus::Invalid);
            Entry current;bool exists{};R_TRY(io.Stat(false,file.path,current,exists));
            // Refresh never silently accepts changed files into an old
            // selection: unmark/reselect them to review their new size.
            R_UNLESS(exists&&!current.directory&&current.size==file.size,nexus::Invalid);
        }
        nexus::Space space{};R_TRY(nexus::QueryDestination(target,space));R_UNLESS(space.free>=32*1024*1024,MAKERESULT(2,30));return progress->ShouldExitResult();
    },[=](Result result){
        if(R_FAILED(result)){App::PushErrorBox(result,"Could not prepare the selected files. Check the NAS and destination. If a selected file was replaced, refresh the folder, unmark it and select it again. Nothing was installed; your selection was kept.");return;}
        std::string message="Install "+std::to_string(files.size())+" files to "+destination+"?\n\n";
        for(const auto& file:files)message+=file.path+"\n";
        message+="\nFiles install one after another directly from the NAS. Original files are kept. The queue stops if an install fails or you cancel. Keep the network and destination connected.";
        App::Push<OptionBox>(message,"Cancel","Install all",0,[=](auto choice){
            if(!choice||*choice!=1)return;
            auto report=std::make_shared<NetworkQueueReport>();
            for(const auto& file:files)report->push_back({file});
            App::Push<ProgressBox>(0,"Install selected files",destination,[=](ProgressBox* progress){return RunInstallQueue(session,files,target,*generation,progress,*report);},[=](Result result){
                if(R_FAILED(result)&&std::none_of(report->begin(),report->end(),[](const auto& row){return row.state==NetworkQueueRecord::State::Failed||row.state==NetworkQueueRecord::State::Cancelled;})){
                    auto failed=std::find_if(report->begin(),report->end(),[](const auto& row){return row.state==NetworkQueueRecord::State::Waiting;});
                    if(failed==report->end())failed=std::prev(report->end());
                    failed->state=result==Result_TransferCancelled?NetworkQueueRecord::State::Cancelled:NetworkQueueRecord::State::Failed;
                    failed->result=result;failed->detail="The queue could not start or finish safely. Check the connection and review this selection before retrying.";
                }
                std::vector<std::string> completed;
                for(const auto& row:*report)if(row.state==NetworkQueueRecord::State::Finished||row.state==NetworkQueueRecord::State::Skipped)completed.push_back(row.file.path);
                if(auto state=browser.lock())state->RemoveSelected(completed);
                Message(InstallQueueSummary(*report,destination));
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void ChooseQueueDestination(SessionPtr session,std::vector<SelectedNetworkFile> files,std::weak_ptr<BrowserModel> browser){
    if(files.empty())return;
    auto menu=std::make_unique<Sidebar>("Install selected files",std::to_string(files.size())+" files selected",Sidebar::Side::RIGHT);
    // The picker captures this reviewed queue. Close it when a destination is
    // chosen so returning from the report cannot replay a stale selection.
    menu->Add<SidebarEntryCallback>("SD Card",[=]{PrepareInstallQueue(session,files,nexus::Target::Sd,browser);},true,"Install every selected file directly from the NAS to the SD card.");
    menu->Add<SidebarEntryCallback>("USB Drive",[=]{PrepareInstallQueue(session,files,nexus::Target::Usb,browser);},true,"Install every selected file directly from the NAS to the connected USB drive.");
    App::Push(std::move(menu));
}
void ReviewCopy(SessionPtr session,const std::string& source,const std::string& directory,u64 generation){
    auto snapshot=std::make_shared<Snapshot>();const auto final=Child(directory,Leaf(source));
    App::Push<ProgressBox>(0,"Checking network files",Leaf(source),[=](ProgressBox* p)->Result{
        R_UNLESS(Safe(source)&&SafeDirectory(directory)&&Safe(final),nexus::Invalid);CopyIO io(session,p,generation,final);R_TRY(io.Check());
        R_TRY(CheckSpelling(io,true,directory));Entry existing;bool exists{};R_TRY(io.Stat(true,final,existing,exists));R_UNLESS(!exists,CopyIO::Conflict);
        R_TRY(Prepare(io,false,source,*snapshot));R_UNLESS(ValidDestination(false,true,*snapshot,final),nexus::Invalid);return io.Preflight(snapshot->bytes);
    },[=](Result rc){if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not prepare this copy. Check the network and USB drive. The destination must have free space and must not already contain this name. Protected folders, links and unsupported names cannot be copied.");return;}
        App::Push<OptionBox>("Copy to USB?\n\nFrom: "+session->name+":"+source+"\nTo: USB:"+final+"\n\n"+std::to_string(snapshot->entries.size())+" items, "+std::to_string(snapshot->bytes)+" bytes.\n\nThe copy is verified before it is published. Existing files are never replaced. The network source is kept.","Cancel","Copy",0,[=](auto choice){
            if(!choice||*choice!=1)return;auto report=std::make_shared<Report>();
            App::Push<ProgressBox>(0,"Copying to USB",Leaf(source),[=](ProgressBox* p){CopyIO io(session,p,generation,final);return nexus::files::Transfer(io,false,true,*snapshot,final,false,*report);},[=](Result result){
                if(R_SUCCEEDED(result)){Message("Copy complete\n\nUSB:"+final+"\n\nThe copy was verified. The network source was kept.");return;}
                auto detail="Copy stopped. The network source was kept.\n\n"+report->detail;
                if(report->published)detail+="\nThe verified copy reached USB:"+final+". The final flush was not confirmed; reconnect and check it before use.";
                if(!report->staged_path.empty())detail+="\nA temporary item may remain at USB:"+report->staged_path+". File manager can review and delete it.";
                detail+="\nCheck the network connection, share permissions, USB drive and available space, then retry.";App::PushErrorBox(result,detail);
            });
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Destination(SessionPtr session,const std::string& source,const std::string& directory,BrowserState browser){
    const bool initial=!browser;if(!browser)browser=std::make_shared<BrowserModel>();
    auto entries=std::make_shared<std::vector<nexusapp::Item>>();auto generation=std::make_shared<u64>();
    App::Push<ProgressBox>(0,"Choose a USB folder",directory,[=](ProgressBox* p){return nexusfiles::ListFiles(p,true,directory,*entries,*generation);},[=](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not read the USB drive. Your current folder is still open. Reconnect it and try again.");return;}
        std::erase_if(*entries,[](const auto& e){return !e.directory||!SafeDirectory(e.path);});
        std::vector<BrowserRow> rows;std::weak_ptr<BrowserModel> weak=browser;
        rows.push_back({"Copy into this folder","Review copy",BrowserKind::Action,[=]{ReviewCopy(session,source,directory,*generation);}});
        for(const auto& item:*entries)rows.push_back({item.name,"Folder",BrowserKind::Folder,[=]{if(auto state=weak.lock())Destination(session,source,item.path,state);}});
        std::function<void()> back;if(directory!="/")back=[=]{if(auto state=weak.lock())Destination(session,source,Parent(directory),state);};
        browser->Replace("Choose USB destination","USB:"+directory,std::move(rows),"Open a folder, then choose Copy into this folder to review and confirm.","No subfolders",std::move(back),[=]{if(auto state=weak.lock())Destination(session,source,directory,state);});
        if(initial)App::Push<ui::menu::network::Browser>(browser);
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Browse(SessionPtr session,const std::string& path,BrowserState browser){
    const bool initial=!browser;if(!browser)browser=std::make_shared<BrowserModel>();
    auto entries=std::make_shared<std::vector<Entry>>();auto skipped=std::make_shared<size_t>();
    App::Push<ProgressBox>(0,"Reading network folder",session->name+":"+path,[=](ProgressBox* p)->Result{
        CopyIO io(session,p);std::vector<std::string> names;R_TRY(io.List(false,path,names));
        for(const auto& name:names){const auto child=Child(path,name);if(!Canonical(child)){++*skipped;continue;}Entry e;bool exists{};const auto rc=io.Stat(false,child,e,exists);if(rc==nexus::Invalid){++*skipped;continue;}R_TRY(rc);R_UNLESS(exists,nexus::Invalid);
            if(session->installing&&!e.directory&&!nexus::package::Supported(e.path))continue;
            entries->push_back(e);}
        std::sort(entries->begin(),entries->end(),[](const auto& a,const auto& b){return a.directory!=b.directory?a.directory>b.directory:Fold(a.path)<Fold(b.path);});return 0;
    },[=](Result rc){if(R_FAILED(rc)){App::PushErrorBox(rc,std::string("Could not read this network folder.")+(initial?"":" Your current folder is still open.")+" Check the connection and share permissions; reconnect if the server disconnected.");return;}
        std::vector<BrowserRow> rows;std::weak_ptr<BrowserModel> weak=browser;
        if(path!="/"&&!session->installing)rows.push_back({"Copy this folder to USB","Choose destination",BrowserKind::Action,[=]{Destination(session,path,"/");}});
        for(const auto& item:*entries)rows.push_back({Leaf(item.path),item.directory?"Folder":utils::formatSizeStorage(item.size),item.directory?BrowserKind::Folder:BrowserKind::File,[=]{
            if(item.directory){if(auto state=weak.lock())Browse(session,item.path,state);}
            else if(nexus::package::Supported(item.path))PickPackage(session,item);else Destination(session,item.path,"/");
        },!item.directory&&nexus::package::Supported(item.path)?item.path:std::string{},item.size});
        std::function<void()> back;if(path!="/")back=[=]{if(auto state=weak.lock())Browse(session,Parent(path),state);};
        std::string help=session->installing?"Mark up to 64 NSP, NSZ, XCI or XCZ files across folders, then choose Install selected. A selects one file.":"Select a game package to install, or another file to copy to USB. Mark packages to install together.";
        if(*skipped)help+=" "+std::to_string(*skipped)+" unsupported items hidden.";
        browser->Replace(session->installing?"Install Games / Browse "+session->protocol:"File manager / "+session->protocol,session->name+":"+path,std::move(rows),help,
            session->installing?"No supported game files in this folder":"This folder is empty",std::move(back),[=]{if(auto state=weak.lock())Browse(session,path,state);});
        browser->SetSelectionAction([session,weak](std::vector<SelectedNetworkFile> files){ChooseQueueDestination(session,std::move(files),weak);});
        if(initial)App::Push<ui::menu::network::Browser>(browser);
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void EditConnectionAfterFailure(const Connection&,bool);
void Connect(const Connection& c,const std::string& password,bool installing=false,bool form_open=false){
    // libnx sockets are process-wide: finish the read-only startup check
    // before a NAS session takes ownership of the network services.
    nexus::updates::StopStartupCheck();
    if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode, or use its HOME app, to connect to a network location.");return;}
    if(!nexus::network::Valid(c)){Message("Enter a location name, server hostname or IPv4 address, and share. SMB needs a share name such as Games. NFS needs the full export path such as /srv/games. Do not put a URL or login details in the server field.");return;}
    session_passwords.Remember(c,password);
    auto session=std::make_shared<Session>();session->name=c.name;session->protocol=c.protocol;session->installing=installing;auto detail=std::make_shared<std::string>();
    common::MountConfig config;config.name=c.name;config.url=nexus::network::Url(c);config.user=c.user;config.pass=password;config.read_only=true;config.no_stat_file=false;config.no_stat_dir=false;
    config.timeout=c.protocol=="SMB"?10:10000;config.extra["domain"]=c.domain;
    App::Push<ProgressBox>(0,"Connecting",c.name,[=](ProgressBox* p)->Result{
        R_TRY(p->ShouldExitResult());
        *detail="Finish PC transfer and connect the Switch to the same network as the server, then try again.";
        R_UNLESS(nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        R_TRY(session->Open(detail.get()));
        Result connected=nexus::Unavailable;
        if(c.protocol=="SMB"){
#ifdef ENABLE_DEVOPTAB_SMB2
            connected=common::ConnectSmbShare(config,session->mount,*detail);
#else
            *detail="This build does not include SMB network locations.";
#endif
        }else{
#ifdef ENABLE_DEVOPTAB_NFS
            connected=common::ConnectNfsShare(config,session->mount,*detail);
#else
            *detail="This build does not include NFS network locations.";
#endif
        }
        R_TRY(connected);
        if(p->ShouldExit())*detail="Connection cancelled. No files were changed.";
        return p->ShouldExitResult();
    },[=](Result rc){if(R_FAILED(rc)){
        // Mount failures also include transient network/server errors. Keep the
        // login and expose the existing form for either retry or explicit edit.
        if(!form_open)EditConnectionAfterFailure(c,installing);
        App::PushErrorBox(rc,*detail+"\n\nYour settings and login were kept. Retry Browse or edit the connection.");return;
    }
        const auto saved=SaveConnectedLocation(c,password);
        Browse(session,"/");
        if(R_FAILED(saved))App::PushErrorBox(saved,"Connected, but this location could not be saved on the SD card. You can browse now; check SD write access or remove an unused saved location before the next boot.");
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
bool PromptNetworkPassword(const Connection& c,std::string& password){
    if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode, or use its HOME app, to connect to a network location.");return false;}
    SwkbdConfig keyboard{};const auto created=swkbdCreate(&keyboard,0);
    if(R_FAILED(created)){App::PushErrorBox(created,"Could not open the password keyboard. Your saved location was kept.");return false;}
    swkbdConfigMakePresetPassword(&keyboard);swkbdConfigSetStringLenMax(&keyboard,128);swkbdConfigSetInitialText(&keyboard,password.c_str());
    swkbdConfigSetHeaderText(&keyboard,"SMB password");
    swkbdConfigSetSubText(&keyboard,c.remember_password?"Remembered on SD without encryption":"Used until Nexus Manager closes");
    const auto guide=c.name+" - "+(c.user.empty()?"Guest":c.user);
    swkbdConfigSetGuideText(&keyboard,guide.c_str());
    char text[512]{};const auto rc=swkbdShow(&keyboard,text,sizeof(text));swkbdClose(&keyboard);
    if(R_SUCCEEDED(rc))password=text;
    memset(text,0,sizeof(text));return R_SUCCEEDED(rc);
}
void OpenSavedInstall(const std::string& name,const std::string& protocol,bool installing=true){
    if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode, or use its HOME app, to connect to a network location.");return;}
    const auto saved=Load();
    const auto found=std::find_if(saved.begin(),saved.end(),[&](const auto& c){return c.name==name&&c.protocol==protocol&&nexus::network::Valid(c);});
    if(found==saved.end()){Message("This saved location changed or was removed. Reopen Install Games to refresh its shortcuts.");return;}
    std::string password;
    if(found->protocol=="SMB"){
        const bool known=KnownPassword(*found,password);
        if(!known&&!found->user.empty()&&!PromptNetworkPassword(*found,password))return;
        session_passwords.Remember(*found,password);
        if(found->remember_password){const auto rc=SavePassword(*found,password);if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not remember the SMB password on this SD card. The login is kept for this session. Check that the saved location has not changed and the SD card is writable, then retry.");return;}}
    }
    Connect(*found,password,installing);
}
class ConnectionForm final:public FormSidebar {
    Connection value,password_target;bool installing{},password_known{};std::string password;SidebarEntryTextInput *name{},*host{},*share{},*user{},*domain{};SidebarEntryTextBase* pass{};
    static std::string Trim(std::string s){const auto first=s.find_first_not_of(" \t\r\n");if(first==s.npos)return {};return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);}
    Connection Read()const{auto c=value;c.name=Trim(name->GetValue());c.host=Trim(host->GetValue());if(c.name.empty())c.name=c.host;c.share=share->GetValue();c.user=user->GetValue();c.domain=domain->GetValue();return c;}
    bool CurrentPassword(const Connection& c,std::string& out){
        if(password_known&&nexus::network::SameEndpoint(password_target,c)){out=password;return true;}
        password_known=KnownPassword(c,password);password_target=c;
        pass->SetValue(password_known?(password.empty()?"Blank password":"Entered"):"Not entered");
        out=password;return password_known;
    }
    void BrowseConnection(){
        if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode, or use its HOME app, to connect to a network location.");return;}
        const auto c=Read();std::string entered;
        if(nexus::network::Valid(c)&&c.protocol=="SMB"){
            const bool known=CurrentPassword(c,entered);
            if(!known&&!c.user.empty()){
                if(!PromptNetworkPassword(c,entered))return;
                password=entered;password_target=c;password_known=true;pass->SetValue(password.empty()?"Blank password":"Entered");
            }
            if(c.remember_password&&SavedEndpoint(c)){
                const auto rc=SavePassword(c,entered);if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not remember this SMB password. Your entries are still here. Check that the SD card is writable, then retry.");return;}
            }
        }
        Connect(c,entered,installing,true);
    }
public:
    explicit ConnectionForm(Connection c={},bool install=false):FormSidebar(install?c.protocol+" connection":"Network location"),value(std::move(c)),installing(install){
        if(!installing)Add<SidebarEntryArray>("Protocol",SidebarEntryArray::Items{"SMB","NFS"},[this](s64& index){value.protocol=index?"NFS":"SMB";},value.protocol=="NFS"?1:0,"SMB: Windows/NAS shares. NFS: exported folders on a NAS or Linux server.");
        name=Add<SidebarEntryTextInput>("Name",value.name,"Location name","Example: Living room PC",0,64,"Optional friendly name. Leave blank to use the server address.");
        host=Add<SidebarEntryTextInput>("Server",value.host,"Server address","Hostname or IPv4 address",1,253,"Example: 192.168.1.20. Do not enter a URL.");
        share=Add<SidebarEntryTextInput>("Share / export",value.share,"Share or export","SMB: Games  |  NFS: /srv/games",1,240,"SMB: the shared folder name. NFS: the full exported folder path.");
        user=Add<SidebarEntryTextInput>("Username",value.user,"SMB username","Leave empty for guest access",-1,128,"SMB only. NFS uses the server's export permissions.");
        password_known=KnownPassword(value,password);password_target=value;
        pass=Add<SidebarEntryTextBase>("Password",password_known?(password.empty()?"Blank password":"Entered"):"Not entered",[this]{const auto c=Read();std::string entered;CurrentPassword(c,entered);if(PromptNetworkPassword(c,entered)){password=entered;password_target=c;password_known=true;pass->SetValue(password.empty()?"Blank password":"Entered");}},"SMB login password. Enable Remember password to connect again after restarting the app.");
        domain=Add<SidebarEntryTextInput>("Domain",value.domain,"SMB domain","Optional",-1,128,"SMB only. Leave empty unless your server requires a domain.");
        Add<SidebarEntryBool>("Remember password",value.remember_password,[this](bool& enabled){
            const auto before=value;value.remember_password=enabled;
            // Apply to the original saved location even if other form fields
            // are being edited; do not silently save an edited endpoint here.
            if(before.protocol=="SMB"&&SavedEndpoint(before)){
                std::string entered;const bool known=password_known&&nexus::network::SameEndpoint(password_target,before)?(entered=password,true):KnownPassword(before,entered);
                const auto rc=Save(value,known?&entered:nullptr);
                if(R_FAILED(rc)){value.remember_password=before.remember_password;enabled=before.remember_password;App::PushErrorBox(rc,"Could not update the saved password preference. Check that the SD card is writable and try again.");}
            }
        },"SMB only. Stores your password on this SD card without encryption. Turn Off to remove the saved password; it can still be used until the app closes.");
        Add<SidebarEntryCallback>("Find shared folders",[this]{const auto c=Read();std::string entered;CurrentPassword(c,entered);BrowseServerShares(c,entered,installing);},false,"Ask this server for its SMB shares or NFS exports. Enter the server and any SMB login first. You can still enter a share manually.");
        Add<SidebarEntryCallback>(installing?"Browse "+value.protocol:"Connect and browse",[this]{BrowseConnection();},false,installing?"Browse NSP, NSZ, XCI or XCZ files and choose one to install to SD Card or USB Drive.":"Browse this share and copy files or folders to USB. The network share is opened read-only.");
        Add<SidebarEntryCallback>("Save location",[this]{const auto c=Read();std::string entered;const bool known=CurrentPassword(c,entered);const auto rc=Save(c,known?&entered:nullptr);if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not save. Your entries are still here. Check the server and share fields, and that the SD card is writable. SMB needs a server address and shared folder name, for example 192.168.1.20 and Games.");return;}if(known)session_passwords.Remember(c,entered);value=c;name->SetValue(c.name);host->SetValue(c.host);App::Notify("Saved "+c.name+(c.protocol=="SMB"&&c.remember_password&&known?" with its login.":"."));},false,"Successful connections are saved automatically. This button also saves settings before connecting. Remember password stores the SMB login on SD without encryption.");
    }
};
void EditConnectionAfterFailure(const Connection& c,bool installing){App::Push<ConnectionForm>(c,installing);}
void BrowseServerShares(Connection c,const std::string& password,bool installing){
    nexus::updates::StopStartupCheck();
    auto session=std::make_shared<Session>();auto shares=std::make_shared<std::vector<std::string>>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Finding shared folders",c.host,[=](ProgressBox* p)->Result{
        R_TRY(p->ShouldExitResult());R_TRY(session->Open(detail.get()));return nexusapp::ListNetworkShares(p,c,password,*shares,*detail);
    },[=](Result rc){
        if(R_FAILED(rc)){App::Push<ConnectionForm>(c,installing);App::PushErrorBox(rc,detail->empty()?"Could not list folders. Enter the shared folder and login manually, or use Find shared folders after entering your login.":*detail);return;}
        auto menu=std::make_unique<Sidebar>(c.protocol+" folders",c.host,Sidebar::Side::RIGHT,1280.f);
        for(const auto& share:*shares)menu->Add<SidebarEntryCallback>(share,[=]{auto selected=c;selected.share=share;selected.name=c.host+" "+share;
            for(auto& ch:selected.name)if(ch=='['||ch==']'||ch=='='||ch==';')ch=' ';
            if(selected.name.size()>64)selected.name=c.host;
            Connect(selected,password,installing);
        },"Connect and browse. Successful connections and your Remember password preference are saved on SD.");
        if(shares->empty())menu->Add<SidebarEntryCallback>("No advertised folders",[detail]{Message(*detail);});
        menu->Add<SidebarEntryCallback>("Enter share / login manually",[=]{App::Push<ConnectionForm>(c,installing);});App::Push(std::move(menu));
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void DiscoverNetwork(bool installing,const std::string& protocol){
    nexus::updates::StopStartupCheck();
    auto session=std::make_shared<Session>();auto servers=std::make_shared<std::vector<nexusapp::SmbServer>>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Find network servers","Scanning the full connected subnet",[=](ProgressBox* p)->Result{
        R_TRY(p->ShouldExitResult());R_UNLESS(nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);R_TRY(session->Open(detail.get()));return nexusapp::DiscoverNetworkServers(p,*servers,*detail,protocol);
    },[=](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"Discovery stopped. Check Wi-Fi or enter the server manually.":*detail);return;}
        auto results=std::make_unique<Sidebar>("Network servers","Choose a server to find its shared folders",Sidebar::Side::RIGHT,1280.f);
        for(const auto& server:*servers)results->Add<SidebarEntryCallback>(server.label,[=]{Connection c;c.protocol=server.protocol;c.name=server.address;c.host=server.address;BrowseServerShares(c,"",installing);},"List shared folders. If the server requires a login, enter it on the connection screen.");
        if(servers->empty())results->Add<SidebarEntryCallback>("No servers found",[detail]{Message(*detail);});
        results->Add<SidebarEntryCallback>("Scan details",[detail]{Message(*detail);});
        results->Add<SidebarEntryCallback>("Enter server manually",[=]{Connection c;if(!protocol.empty())c.protocol=protocol;App::Push<ConnectionForm>(c,installing);});App::Push(std::move(results));
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
class NetworkLocationsSidebar final:public Sidebar {
    const std::string protocol_;const bool installing_;bool focused_once{};
    void Populate(){
    const auto protocol=protocol_;const auto installing=installing_;auto* menu=this;
    menu->Add<SidebarEntryCallback>(protocol.empty()?"Scan for SMB and NFS servers":"Scan for "+protocol+" servers",[=]{DiscoverNetwork(installing,protocol);},"Scan the entire connected IPv4 subnet. Press B to stop and use the servers found so far.");
    menu->Add<SidebarEntryCallback>("Add SMB location",[=]{Connection c;c.protocol="SMB";App::Push<ConnectionForm>(c,installing);});
    menu->Add<SidebarEntryCallback>("Add NFS location",[=]{Connection c;c.protocol="NFS";App::Push<ConnectionForm>(c,installing);});
    for(const auto& c:Load())if((protocol.empty()||c.protocol==protocol)&&nexus::network::Valid(c)){
        auto* row=menu->Add<SidebarEntryCallback>("["+c.protocol+"] "+c.name,[c,installing]{OpenSavedInstall(c.name,c.protocol,installing);},"A opens directly with your saved login. X edits or forgets this location.");
        row->SetAction(Button::X,Action{"Edit",[c,installing]{
            auto actions=std::make_unique<Sidebar>(c.name,Sidebar::Side::RIGHT);
            actions->Add<SidebarEntryCallback>("Edit connection",[=]{App::Push<ConnectionForm>(c,installing);},true);
            actions->Add<SidebarEntryCallback>("Forget location",[c]{App::Push<OptionBox>("Forget "+c.name+" and its saved login? Server files are kept.","Cancel","Forget",0,[c](auto choice){if(choice&&*choice==1){if(R_SUCCEEDED(Forget(c)))App::Notify("Location forgotten.");else Message("Could not remove the saved connection from SD.");}});},true);App::Push(std::move(actions));
        }});
    }
    }
public:
    NetworkLocationsSidebar(const std::string& protocol,bool installing):Sidebar(protocol.empty()?"SMB / NFS":"Browse "+protocol,"Saved servers open directly. New connections are remembered on SD.",Sidebar::Side::RIGHT,1280.f),protocol_(protocol),installing_(installing){Populate();}
    void OnFocusGained() noexcept override {
        // Saving keeps the form alive. Reload once the user returns to this
        // list so new/edited/forgotten locations appear without a manual refresh.
        if(focused_once){ClearEntries();Populate();}
        focused_once=true;Sidebar::OnFocusGained();
    }
};
void Locations(const std::string& protocol,bool installing){
    if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode (hold R while starting a game), or use its HOME app, to browse network locations.");return;}
    App::Push<NetworkLocationsSidebar>(protocol,installing);
}
}
void DisplayNetworkLocations(){Locations({},false);}
void DisplayNetworkInstallLocations(const std::string& protocol){
    if(!protocol.empty()&&protocol!="SMB"&&protocol!="NFS"){Message("Choose SMB or NFS.");return;}
    Locations(protocol,true);
}
void AddNetworkInstallShortcuts(ui::Sidebar& menu){
    size_t count{};
    for(const auto& c:Load()){
        if(!nexus::network::Valid(c))continue;
        if(count++>=64)break;
        menu.Add<SidebarEntryCallback>("["+c.protocol+"] "+c.name,
            [name=c.name,protocol=c.protocol]{OpenSavedInstall(name,protocol);},false,
            "Open this saved network location. Choose a game file, then install to SD Card or USB Drive.");
    }
}
}
