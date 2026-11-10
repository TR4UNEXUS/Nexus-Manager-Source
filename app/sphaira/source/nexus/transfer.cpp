// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer.hpp"
#include "nexus/transfer_policy.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/nx_Client.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
#include "utils/devoptab.hpp"
#include <dirent.h>
#include <haze.h>
#include <ftpsrv.h>
#include <ftpsrv_vfs.h>
#include <nx/vfs_nx.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <memory>
#include <deque>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace nexus::transfer {
namespace {
constexpr Result Bad=MAKERESULT(Module_Libnx,LibnxError_BadInput);
FsFileSystem storage{};bool fsOpen{},sockets{},network{},ftpOpen{},mtpOpen{},gamesMounted{},awake{},refreshAwake{};
Mode mode=Mode::Off;std::atomic_bool failed{};
std::atomic<u64> sent{},received{},completed{};std::atomic<Result> lastError{};
std::string endpoint;std::mutex fsMutex,queueMutex;
std::deque<InstallJob> jobs;bool prepared{},usbAvailable{};unsigned omitted{};
std::atomic_bool installBusy{},installFailed{};std::atomic<u64> installed{};
std::string startupMessage;
// libnx sends FS_MAX_PATH bytes, not strlen(path)+1. Never hand it a short
// string's c_str(), a string literal, or truncated/uninitialized storage.
template<class Call> Result SdPath(std::string_view name,Call call){
    R_UNLESS(!name.empty() && name.size()<FS_MAX_PATH && name.find('\0')==name.npos,Bad);
    char path[FS_MAX_PATH]{};std::memcpy(path,name.data(),name.size());return call(path);
}
Result StartupStep(const char* phase,const char* message,Result rc,u64 value=0){
    nexus::Log(phase,rc,value);if(R_FAILED(rc)){startupMessage=message;lastError=rc;}return rc;
}
struct Area;
struct File {Area* owner{};FsFile fs{};nexus::Client client;u64 generation{};s64 offset{},expected{-1};bool usb{},write{},error{};std::string path;};
struct Dir {Area* owner{};FsDir fs{};bool usb{};std::vector<nexus::Entry> entries;size_t index{};std::string path;FsDirectoryEntry last{};};
struct Area {
const Use use;const u64 generation;
Area(Use purpose,u64 pin):use(purpose),generation(pin){}
std::map<std::string,s64> pending; // MTP declares a size before opening the object.
bool Path(const char* path,std::string& out,bool mut=false,bool dir=false){return lifecycle::CanPoll() && path && ResolveVolume(use,path,out,mut,dir);}
Result Type(const std::string& path,FsDirEntryType* type){
    if(!Usb(use))return SdPath(path,[&](const auto& p){return fsFsGetEntryType(&storage,p,type);});
    nexus::Client c;nexus::Info i{};R_TRY(c.Check(generation));R_TRY(c.Stat(generation,path,i));*type=i.directory?FsDirEntryType_Dir:FsDirEntryType_File;return 0;
}
Result Capacity(bool total,s64* out){
    if(!Usb(use))return SdPath("/",[&](const auto& p){return total?fsFsGetTotalSpace(&storage,p,out):fsFsGetFreeSpace(&storage,p,out);});
    nexus::Client c;nexus::Space s{};R_TRY(c.GetSpace(generation,s));const auto n=total?s.total:s.free;R_UNLESS(n<=INT64_MAX,Bad);*out=n;return 0;
}
Result Missing(const std::string& p){
    // A failed lookup is NOT automatically evidence of absence. The USB owner
    // uses module 140 / description 205 for path-not-found; all other errors stop.
    FsDirEntryType t{};const auto rc=Type(p,&t);
    return rc==(Usb(use)?MAKERESULT(140,205):MAKERESULT(2,1))?0:R_FAILED(rc)?rc:MAKERESULT(2,2);
}
Result MakeDirectory(const std::string& p){if(!Usb(use))return SdPath(p,[&](const auto& path){return fsFsCreateDirectory(&storage,path);});nexus::Client c;return c.CreateDirectory(generation,p);}
Result Mk(const char* p){
    FsDirEntryType t{};auto rc=Type(p,&t);
    // These are fixed application folders, never user filenames or secrets.
    const auto phase=std::string(Usb(use)?"pc-usb-folder:":"pc-sd-folder:")+p;
    if(R_SUCCEEDED(rc) && t==FsDirEntryType_Dir){nexus::Log(phase.c_str(),0);return 0;}
    if(R_SUCCEEDED(rc))return StartupStep(phase.c_str(),"A file is blocking a required transfer folder. Existing files were kept. Check nexus.log for the folder name.",MAKERESULT(2,2));
    if(rc!=(Usb(use)?MAKERESULT(140,205):MAKERESULT(2,1)))
        return StartupStep(phase.c_str(),"A transfer folder could not be read. Reopen the app and try again; check nexus.log if it repeats.",rc);
    rc=MakeDirectory(p);
    return StartupStep(phase.c_str(),"A transfer folder could not be created. Check storage space and write access; see nexus.log for the folder name.",rc);
}
Result Prepare(){
    if(Usb(use)){nexus::Client c;R_TRY(c.Check(generation));}else R_UNLESS(fsOpen,Bad);
    R_TRY(Mk("/switch"));R_TRY(Mk("/switch/nexusmanager"));
    if(use!=Use::SaveDownloads && use!=Use::SaveUploads)R_TRY(Mk(Root));
    return Mk(Base(use));
}
Result Create(const std::string& p,s64 size){
    R_UNLESS(size>=0 && size<=s64(1ULL<<40),Bad);
    if(Installing(use)){std::scoped_lock lock(queueMutex);R_UNLESS(!installFailed && jobs.size()+pending.size()<32,Bad);}
    s64 space{};R_TRY(Capacity(false,&space));
    R_UNLESS(space>size && space-size>=16*1024*1024,Bad);
    R_TRY(Missing(p));R_TRY(Missing(p+".partial"));
    if(Usb(use)){nexus::Client c;return c.CreateFile(generation,p+".partial",size);}
    return SdPath(p+".partial",[&](const auto& path){return fsFsCreateFile(&storage,path,size,FsCreateOption_BigFile);});
}
Result Open(const std::string& path,bool write,s64 expected,File** output){
    auto f=std::make_unique<File>();f->owner=this;f->path=path;f->write=write;f->expected=expected;
    f->usb=Usb(use);f->generation=generation;
    const auto name=write?path+".partial":path;
    if(f->usb){nexus::Info i{};R_TRY(f->client.Check(f->generation));R_TRY(f->client.Stat(f->generation,name,i));R_UNLESS(!i.directory,Bad);}
    else R_TRY(SdPath(name,[&](const auto& p){return fsFsOpenFile(&storage,p,write?FsOpenMode_Write|FsOpenMode_Append:FsOpenMode_Read,&f->fs);}));
    *output=f.release();return 0;
}
Result Size(File* f,s64* size){
    R_UNLESS(f && f->owner==this,Bad);if(!f->usb)return fsFileGetSize(&f->fs,size);
    nexus::Info i{};R_TRY(f->client.Stat(f->generation,f->path+(f->write?".partial":""),i));R_UNLESS(!i.directory && i.size<=INT64_MAX,Bad);*size=i.size;return 0;
}
Result Write(File* f,s64 off,const void* buf,u64 size){
    if(!lifecycle::CanPoll()){if(f)f->error=true;return Bad;}
    if(!f || f->owner!=this || !f->write)return Bad;
    if(!CanWrite(f->offset,f->expected,off,size,f->error)){f->error=true;return Bad;}
    if(u64(off)>1ULL<<40 || size>(1ULL<<40)-u64(off)){f->error=true;return Bad;}
    auto rc=f->usb?f->client.Write(f->generation,f->path+".partial",off,buf,size):fsFileWrite(&f->fs,off,buf,size,FsWriteOption_None);
    if(R_FAILED(rc)){f->error=true;lastError=rc;return rc;}
    f->offset+=size;received+=size;return 0;
}
Result Read(File* f,s64 off,void* buf,u64 size,u64* done){
    R_UNLESS(lifecycle::CanPoll(),Bad);
    R_UNLESS(f && f->owner==this && !f->write && off>=0,Bad);
    s64 length{};R_TRY(Size(f,&length));R_UNLESS(off<=length,Bad);size=std::min<u64>(size,length-off);*done=0;
    auto rc=f->usb?(size?f->client.Read(f->generation,f->path,off,buf,size,*done):Result(0)):fsFileRead(&f->fs,off,buf,size,FsReadOption_None,done);
    if(R_SUCCEEDED(rc))sent+=*done;else lastError=rc;
    return rc;
}
Result Close(File* f,bool complete){
    if(!f)return 0;
    R_UNLESS(f->owner==this,Bad);
    Result rc=0;
    if(f->write){rc=f->usb?f->client.Flush(f->generation):fsFileFlush(&f->fs);if(R_FAILED(rc))f->error=true;}
    if(!f->usb)fsFileClose(&f->fs);
    if(f->write && CanPublish(f->offset,f->expected,complete,f->error) && lifecycle::CanPoll()){
        // Rename never replaces a completed file. Aborted transfers retain .partial.
        rc=Missing(f->path);
        if(R_SUCCEEDED(rc))rc=f->usb?f->client.RenameNew(f->generation,f->path+".partial",f->path):SdPath(f->path+".partial",[&](const auto& from){return SdPath(f->path,[&](const auto& to){return fsFsRenameFile(&storage,from,to);});});
        if(R_SUCCEEDED(rc))rc=f->usb?f->client.Flush(f->generation):fsFsCommit(&storage);
        if(R_SUCCEEDED(rc)){
            ++completed;
            if(Installing(use)){std::scoped_lock lock(queueMutex);if(jobs.size()>=32 || !f->offset)rc=Bad;else jobs.push_back({f->usb,f->generation,u64(f->offset),f->path});}
        }
    }else if(f->write && complete)rc=Bad;
    if(R_FAILED(rc))lastError=rc;
    delete f;return rc;
}
Result OpenDir(const std::string& path,Dir** out){
    auto d=std::make_unique<Dir>();d->owner=this;d->usb=Usb(use);d->path=path;
    if(d->usb){nexus::Client c;R_TRY(c.List(generation,path,d->entries));}
    else R_TRY(SdPath(path,[&](const auto& p){return fsFsOpenDirectory(&storage,p,FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&d->fs);}));
    *out=d.release();return 0;
}
Result ReadDir(Dir* d,s64* count,FsDirectoryEntry* entry){
    R_UNLESS(d && d->owner==this && lifecycle::CanPoll(),Bad);*count=0;
    for(;;){
        if(d->usb){nexus::Client c;R_TRY(c.Check(generation));if(d->index==d->entries.size())return 0;
            const auto& e=d->entries[d->index++];*entry={};std::snprintf(entry->name,sizeof(entry->name),"%s",e.name);entry->type=e.directory?FsDirEntryType_Dir:FsDirEntryType_File;entry->file_size=e.size;*count=1;}
        else R_TRY(fsDirRead(&d->fs,count,1,entry));
        if(!*count)return 0;
        std::string resolved;auto relative=d->path.substr(std::strlen(Base(use)));relative+="/";relative+=entry->name;
        if(ResolveFor(use,relative,resolved,false,entry->type==FsDirEntryType_Dir))return 0;
    }
}
void CloseDir(Dir* d){if(d){if(!d->usb)fsDirClose(&d->fs);delete d;}}
};
class Proxy final:public haze::FileSystemProxyImpl,public Area {
public:
    Proxy(Use purpose,u64 pin=0):Area(purpose,pin){}
    bool Path(const char* p,std::string& out,bool mut=false,bool dir=false){
        // libhaze::FileSystemProxy strips one leading slash. FTP does not.
        // Restore it only at this MTP boundary; the common policy still checks
        // the exact storage name, traversal, extension and immutable target.
        if(!p || !*p)return false;
        const auto path=p[0]=='/'?std::string(p):std::string("/")+p;
        const bool ok=Area::Path(path.c_str(),out,mut,dir);
        if(!ok)nexus::Log("pc-mtp-path-rejected",Bad,static_cast<u64>(use));
        return ok;
    }
    const char* GetName()const override{return VolumeName(use);}
    const char* GetDisplayName()const override{return VolumeName(use);}
    bool IsReadOnly()override{return use==Use::SaveDownloads;}
    Result GetTotalSpace(const char*,s64* out)override{return Capacity(true,out);}
    Result GetFreeSpace(const char*,s64* out)override{return Capacity(false,out);}
    Result GetEntryType(const char* p,haze::FileAttrType* out)override{
        std::string name;R_UNLESS(Path(p,name),Bad);FsDirEntryType type{};R_TRY(Type(name,&type));
        *out=type==FsDirEntryType_Dir?haze::FileAttrType_DIR:haze::FileAttrType_FILE;return 0;
    }
    Result CreateFile(const char* p,s64 size)override{
        std::scoped_lock lock(fsMutex);std::string name;R_UNLESS(Path(p,name,true),Bad);
        R_UNLESS(pending.size()<64,Bad);R_TRY(Create(name,size));pending[name]=size;return 0;
    }
    Result DeleteFile(const char*)override{return Bad;} // PC cannot delete existing exports.
    Result RenameFile(const char*,const char*)override{return Bad;}
    Result OpenFile(const char* p,haze::FileOpenMode m,haze::File* out)override{
        std::scoped_lock lock(fsMutex);std::string name;bool write=m==haze::FileOpenMode_WRITE;
        R_UNLESS(Path(p,name,write),Bad);s64 size=-1;
        if(write){auto it=pending.find(name);R_UNLESS(it!=pending.end(),Bad);size=it->second;pending.erase(it);}
        File* f{};R_TRY(Open(name,write,size,&f));out->impl=f;return 0;
    }
    Result GetFileSize(haze::File* f,s64* out)override{return Size(static_cast<File*>(f->impl),out);}
    Result SetFileSize(haze::File* f,s64 n)override{
        auto* file=static_cast<File*>(f->impl);R_UNLESS(file && file->write && file->offset==0 && n>=0 && n<=s64(1ULL<<40),Bad);
        s64 space{};R_TRY(Capacity(false,&space));
        s64 current{};R_TRY(Size(file,&current));
        R_UNLESS(n<=current || (space>n-current && space-(n-current)>=16*1024*1024),Bad);
        // USB writes extend the staged file. Never truncate/recreate it here.
        if(file->usb){R_UNLESS(n>=current,Bad);}else R_TRY(fsFileSetSize(&file->fs,n));file->expected=n;return 0;
    }
    Result ReadFile(haze::File* f,s64 off,void* b,u64 n,u64* done)override{return Read(static_cast<File*>(f->impl),off,b,n,done);}
    Result WriteFile(haze::File* f,s64 off,const void* b,u64 n)override{return Write(static_cast<File*>(f->impl),off,b,n);}
    void CloseFile(haze::File* f)override{Close(static_cast<File*>(f->impl),false);f->impl=nullptr;}
    Result FinishFile(haze::File* f)override{auto* file=static_cast<File*>(f->impl);f->impl=nullptr;return Close(file,true);}
    Result CreateDirectory(const char* p)override{std::string name;R_UNLESS(Path(p,name,true,true),Bad);return MakeDirectory(name);}
    Result DeleteDirectoryRecursively(const char*)override{return Bad;}
    Result RenameDirectory(const char*,const char*)override{return Bad;}
    Result OpenDirectory(const char* p,haze::Dir* out)override{
        std::string name;R_UNLESS(Path(p,name,false,true),Bad);Dir* d{};R_TRY(OpenDir(name,&d));out->impl=d;return 0;
    }
    Result ReadDirectory(haze::Dir* d,s64* count,size_t max,haze::DirEntry* entries)override{
        *count=0;auto e=std::make_unique<FsDirectoryEntry>();
        while(size_t(*count)<max){s64 n{};R_TRY(ReadDir(static_cast<Dir*>(d->impl),&n,e.get()));if(!n)break;
            if(IsPartial(e->name))continue;
            std::snprintf(entries[*count].name,sizeof(entries[*count].name),"%s",e->name);++*count;
        }return 0;
    }
    Result GetDirectoryEntryCount(haze::Dir* d,s64* out)override{
        Dir* scan{};R_TRY(OpenDir(static_cast<Dir*>(d->impl)->path,&scan));
        *out=0;auto e=std::make_unique<FsDirectoryEntry>();Result rc=0;
        while(lifecycle::CanPoll()){s64 n{};rc=ReadDir(scan,&n,e.get());if(R_FAILED(rc)||!n)break;if(!IsPartial(e->name))++*out;}
        CloseDir(scan);return lifecycle::CanPoll()?rc:Bad;
    }
    void CloseDirectory(haze::Dir* d)override{CloseDir(static_cast<Dir*>(d->impl));d->impl=nullptr;}
};
std::vector<std::shared_ptr<Proxy>> areas;
std::vector<VfsNxCustomPath> FtpExtensions();
}
Mode Active(){return mode;}
std::string StartupMessage(){return (startupMessage.empty()?"PC transfer could not start. Reopen the app and try again.":startupMessage)+std::string("\n\nNo installation was started. Details: switch/nexusmanager/nexus.log");}
std::string SharedFolder(){return usbAvailable?(mode==Mode::Ftp?"PC: sd-install:/ or usb-install:/ | games:/ to export. B disconnects.":"PC: SD Install or USB Install | Games to export. B disconnects."):"USB not shared. Mount it first and reconnect. SD and Games remain available.";}
bool TakeInstall(InstallJob& job){std::scoped_lock lock(queueMutex);if(mode==Mode::Off || installBusy || installFailed || jobs.empty() || !lifecycle::CanPoll())return false;job=jobs.front();jobs.pop_front();installBusy=true;return true;}
void FinishInstall(Result rc){installBusy=false;if(mode!=Mode::Off)refreshAwake=true;if(R_FAILED(rc)){lastError=rc;installFailed=true;}else ++installed;}
void Stop(){
    if(ftpOpen)sphaira::ftpsrv::Exit();
    if(mtpOpen)sphaira::libhaze::Exit();
    ftpOpen=mtpOpen=false;
    // Join both protocol workers before dropping filesystem/catalogue owners.
    areas.clear();prepared=usbAvailable=false;
    if(gamesMounted)sphaira::devoptab::UmountNeworkDevice("games:/");
    gamesMounted=false;
    if(fsOpen){auto rc=fsFsCommit(&storage);if(R_FAILED(rc))lastError=rc;fsFsClose(&storage);}fsOpen=false;
    if(network)nifmExit();
    network=false;
    if(sockets)socketExit();
    sockets=false;
    if(awake)appletSetAutoSleepDisabled(false);
    awake=refreshAwake=false;
    if(mode!=Mode::Off)nexus::Log("manager-transfer-stop",lastError.load());
    {std::scoped_lock lock(queueMutex);jobs.clear();}endpoint.clear();mode=Mode::Off;
}
Result PrepareSession(sphaira::ui::ProgressBox* p){
    startupMessage.clear();nexus::Log("pc-D10-sphaira-prepare-begin",0);
    R_TRY(StartupStep("pc-prepare-state","Close the current transfer or installation first. Use full-memory mode, not Album.",p && mode==Mode::Off && !installBusy && !sphaira::App::IsApplet()?0:Bad));
    Stop();omitted=0;failed=false;lastError=0;
    auto run=[&]()->Result{
        R_TRY(p->ShouldExitResult());R_UNLESS(lifecycle::CanPoll(),Bad);
        nexus::Space sd{};R_TRY(StartupStep("pc-sd-capacity","The physical SD card could not be checked. Reopen the app in full-memory mode and check that the SD is available.",nexus::QueryPhysicalSd(sd)));
        R_TRY(StartupStep("pc-sd-open","The SD card could not be opened for PC transfer. Reopen the app and try again.",fsOpenSdCardFileSystem(&storage)));fsOpen=true;
        u64 usbPin{};nexus::Client c;nexus::Status status{};
        if(R_SUCCEEDED(nexus::CheckUsbFirmware()) && R_SUCCEEDED(nexus::CheckUsbMutationAllowed()) && R_SUCCEEDED(c.Query(status)) && status.Available())usbPin=status.generation;
        R_TRY(StartupStep("pc-games-mount","Sphaira's installed-game browser could not be mounted. Reopen Manager in full-memory mode.",sphaira::devoptab::MountGameAll()));gamesMounted=true;
        // Open the actual upstream devoptab now: this initializes its title/NS/
        // ES services before any host requests, and fails visibly if unavailable.
        auto* games=opendir("games:/");
        R_TRY(StartupStep("pc-games-open","Installed games could not be read. Reopen Manager and check nexus.log.",games?0:Bad));
        size_t count{};while(auto* entry=readdir(games)){
            if(std::strcmp(entry->d_name,".") && std::strcmp(entry->d_name,".."))++count;
            if(R_FAILED(p->ShouldExitResult())){closedir(games);return Bad;}
        }
        closedir(games);nexus::Log("pc-sphaira-games-listed",0,count);
        for(auto purpose:{Use::FilesUsb,Use::InstallSd,Use::InstallUsb,Use::SaveDownloads,Use::SaveUploads}){
            R_TRY(p->ShouldExitResult());R_UNLESS(lifecycle::CanPoll(),Bad);p->SetTitle(VolumeName(purpose));
            const bool usb=Usb(purpose);if(usb && !usbPin)continue;
            auto area=std::make_shared<Proxy>(purpose,usb?usbPin:0);
            Result rc=area->Prepare();
            nexus::Log("pc-volume-ready",rc,static_cast<u64>(purpose));
            if(R_FAILED(rc)){
                nexus::Log("pc-volume-unavailable",rc,static_cast<u64>(purpose));++omitted;
                // A missing optional USB/export view must not break SD transfer.
                if(!usb)return rc;
                startupMessage.clear();lastError=0;
                continue;
            }
            if(purpose==Use::InstallUsb)usbAvailable=true;
            areas.push_back(std::move(area));
        }
        R_TRY(p->ShouldExitResult());R_UNLESS(lifecycle::CanPoll() && !areas.empty(),Bad);return 0;
    };
    auto rc=run();nexus::Log("pc-prepare-complete",rc,areas.size());if(R_FAILED(rc)){lastError=rc;Stop();return rc;}prepared=true;return 0;
}
Result Start(Mode next){
    R_TRY(StartupStep("pc-start-state","The transfer session is no longer ready. Keep Nexus Manager open and try connecting again.",mode==Mode::Off && prepared && !areas.empty() && !installBusy && (next==Mode::Cable || next==Mode::Ftp) && lifecycle::CanPoll() && !sphaira::App::IsApplet()?0:Bad));
    failed=false;installFailed=false;installed=sent=received=completed=0;lastError=0;
    auto init=[&]()->Result{
        if(next==Mode::Cable){
            haze::FsEntries entries;
            for(auto& a:areas)entries.push_back(a);
            R_TRY(StartupStep("pc-mtp-sphaira-init","USB cable sharing could not start. Disconnect other USB-transfer apps, reopen Manager and use a data cable directly to the PC.",sphaira::libhaze::Init(&entries)?0:Bad));mtpOpen=true;
        }else{
            R_TRY(StartupStep("pc-ftp-sockets","Network sockets could not be opened. Close other transfer apps and reopen Manager.",socketInitializeDefault()));sockets=true;
            R_TRY(StartupStep("pc-ftp-network","The Switch network service could not be opened. Reopen Manager and try again.",nifmInitialize(NifmServiceType_User)));network=true;
            u32 ip{};R_TRY(StartupStep("pc-ftp-address","No network address is available. Connect the Switch and PC to the same trusted network, then try again.",nifmGetCurrentIpAddress(&ip)));
            R_TRY(StartupStep("pc-ftp-address-ready","The Switch is not connected to a network. Connect it before starting FTP.",ip?0:Bad));
            char address[INET_ADDRSTRLEN]{};R_UNLESS(inet_ntop(AF_INET,&ip,address,sizeof(address)),Bad);
            endpoint=std::string("ftp://")+address+":5000";
            auto extensions=FtpExtensions();
            R_TRY(StartupStep("pc-ftp-sphaira-init","Sphaira FTP could not start. Reopen Manager and try again.",sphaira::ftpsrv::Init(&extensions)?0:Bad));ftpOpen=true;
        }return 0;
    };
    auto rc=init();if(R_FAILED(rc)){lastError=rc;Stop();return rc;}
    mode=next;appletSetAutoSleepDisabled(true);awake=refreshAwake=true;
    nexus::Log(next==Mode::Cable?"manager-transfer-mtp-start":"manager-transfer-ftp-start",0);return 0;
}
void Poll(){
    PollInstall();if(mode==Mode::Cable && !haze::IsRunning())failed=true;
    if(mode==Mode::Ftp && !sphaira::ftpsrv::IsRunning()){lastError=Bad;nexus::Log("pc-ftp-stopped",Bad);failed=true;}
    if(mode!=Mode::Off && (!lifecycle::CanPoll() || failed.load()))Stop();
    // ProgressBox resets auto-sleep AFTER its completion callback. Reapply once
    // on the next UI tick, after PrepareSession/installation's box was destroyed.
    if(mode!=Mode::Off && refreshAwake){appletSetAutoSleepDisabled(true);refreshAwake=false;}
}
std::string Status(){
    if(mode==Mode::Off)return failed?"Connection stopped\nCheck your connection and try again.\nIncomplete uploads stay unfinished; existing files are kept.":"Ready when you are\nChoose USB cable or Wi-Fi above to connect your PC.\nNo connection is running in the background.";
    std::string s=mode==Mode::Cable?"USB cable is ready\nConnect directly to your PC, then open Nintendo Switch in its file browser.":endpoint+"\nChoose Anonymous in your FTP app. No login details to enter.\nTrusted network only: anyone on it can access your SD files.";
    return "Sphaira SD and Games + "+std::to_string(areas.size())+" Nexus destinations"+(omitted?" / some unavailable":"")+"\n"+s+"\nUploaded: "+std::to_string(completed.load())+"    Installed: "+std::to_string(installed.load())+"    Received: "+std::to_string(received.load()/1048576)+" MiB"+(installFailed?"\nInstall queue paused. Your uploaded packages are kept.":"\nChoose a destination on your PC. B disconnects.")+(lastError.load()?"\nA file failed. Check space/connection; partial uploads are not installed.":"");
}
}

// Sphaira owns FTP routing, sdmc:/ and games:/. Only the Nexus firmware,
// verified install staging and confirmed-save-import bridges live here.
namespace nexus::transfer {
namespace {
const char* FtpName(Use use){
    switch(use){case Use::FilesUsb:return "usb";case Use::InstallSd:return "sd-install";
    case Use::InstallUsb:return "usb-install";case Use::SaveDownloads:return "save-backups";
    case Use::SaveUploads:return "save-uploads";default:return nullptr;}
}
Area* Route(const char* path,std::string& name,bool write=false,bool directory=false){
    if(!path)return nullptr;
    std::string_view p(path);if(p.starts_with('/'))p.remove_prefix(1);
    for(auto& a:areas){const auto prefix=std::string(FtpName(a->use))+":";
        if(!p.starts_with(prefix) || (p.size()>prefix.size() && p[prefix.size()]!='/'))continue;
        const auto mapped=std::string("/")+VolumeName(a->use)+std::string(p.substr(prefix.size()));
        if(a->Area::Path(mapped.c_str(),name,write,directory))return a.get();
    }
    return nullptr;
}
static int ftp_result(Result rc){if(R_SUCCEEDED(rc))return 0;errno=EIO;return -1;}
int bridge_open(void* user,const char* p,FtpVfsOpenMode m){
    auto& f=*static_cast<File**>(user);f=nullptr;std::string name;auto* a=Route(p,name,m!=FtpVfsOpenMode_READ);if(!a || m==FtpVfsOpenMode_APPEND){errno=EACCES;return -1;}
    if(m==FtpVfsOpenMode_WRITE && R_FAILED(a->Create(name,0))){errno=EEXIST;return -1;}
    return ftp_result(a->Open(name,m==FtpVfsOpenMode_WRITE,-1,&f));
}
int bridge_read(void* user,void* b,size_t n){auto file=*static_cast<File**>(user);u64 done{};if(!file || ftp_result(file->owner->Read(file,file->offset,b,n,&done)))return -1;file->offset+=done;return done;}
int bridge_write(void* user,const void* b,size_t n){auto file=*static_cast<File**>(user);return !file || ftp_result(file->owner->Write(file,file->offset,b,n))?-1:int(n);}
int bridge_seek(void* user,const void*,size_t,size_t off){auto file=*static_cast<File**>(user);if(!file || file->write || off>INT64_MAX){errno=EACCES;return -1;}file->offset=off;return 0;}
int bridge_finish(void* user){auto& f=*static_cast<File**>(user);auto* file=f;f=nullptr;return file?ftp_result(file->owner->Close(file,true)):0;}
int bridge_close(void* user){auto& f=*static_cast<File**>(user);auto* file=f;f=nullptr;return file?ftp_result(file->owner->Close(file,false)):0;}
int bridge_isfile_open(void* user){return *static_cast<File**>(user)!=nullptr;}
int bridge_opendir(void* user,const char* p){
    auto& d=*static_cast<Dir**>(user);d=nullptr;
    std::string name;auto* a=Route(p,name,false,true);if(!a){errno=EACCES;return -1;}return ftp_result(a->OpenDir(name,&d));
}
const char* bridge_readdir(void* user,void*){
    auto dir=*static_cast<Dir**>(user);if(!dir || !nexus::lifecycle::CanPoll())return nullptr;
    s64 n{};if(ftp_result(dir->owner->ReadDir(dir,&n,&dir->last)) || !n)return nullptr;
    return dir->last.name;
}
int bridge_dirlstat(void* user,const void*,const char*,struct stat* st){const auto dir=*static_cast<Dir**>(user);if(!dir)return -1;const auto& e=dir->last;std::memset(st,0,sizeof(*st));st->st_mode=(e.type==FsDirEntryType_Dir?S_IFDIR|0555:S_IFREG|0444);st->st_nlink=1;st->st_size=e.file_size;return 0;}
int bridge_closedir(void* user){auto& d=*static_cast<Dir**>(user);if(d)d->owner->CloseDir(d);d=nullptr;return 0;}
int bridge_isdir_open(void* user){return *static_cast<Dir**>(user)!=nullptr;}
int bridge_stat(const char* p,struct stat* st){std::memset(st,0,sizeof(*st));st->st_nlink=1;
    std::string name;auto* a=Route(p,name);if(!a){errno=EACCES;return -1;}FsDirEntryType t{};if(ftp_result(a->Type(name,&t)))return -1;st->st_mode=t==FsDirEntryType_Dir?S_IFDIR|0755:S_IFREG|0444;
    if(t==FsDirEntryType_File){File* f{};if(ftp_result(a->Open(name,false,-1,&f)))return -1;s64 n{};auto rc=a->Size(f,&n);a->Close(f,false);if(ftp_result(rc))return -1;st->st_size=n;}return 0;}
int bridge_mkdir(const char* p){std::string name;auto* a=Route(p,name,true,true);if(!a){errno=EACCES;return -1;}return ftp_result(a->MakeDirectory(name));}
int bridge_denied(const char*){errno=EACCES;return -1;}
int bridge_rename(const char*,const char*){errno=EACCES;return -1;}
FtpVfs bridge_vfs={
    .open=bridge_open,.read=bridge_read,.write=bridge_write,.seek=bridge_seek,.close=bridge_close,
    .isfile_open=bridge_isfile_open,.isfile_ready=bridge_isfile_open,
    .opendir=bridge_opendir,.readdir=bridge_readdir,.dirlstat=bridge_dirlstat,
    .closedir=bridge_closedir,.isdir_open=bridge_isdir_open,
    .stat=bridge_stat,.lstat=bridge_stat,.mkdir=bridge_mkdir,.unlink=bridge_denied,
    .rmdir=bridge_denied,.rename=bridge_rename,.finish=bridge_finish
};
std::vector<VfsNxCustomPath> FtpExtensions(){
    std::vector<VfsNxCustomPath> result;
    for(const auto& a:areas)result.push_back({.name=FtpName(a->use),.user=nullptr,.func=&bridge_vfs});
    return result;
}
}
}
