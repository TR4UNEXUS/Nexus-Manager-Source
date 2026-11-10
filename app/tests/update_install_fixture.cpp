// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the unchanged production Install function with real temporary-file IO
// and SHA-256. Mocks represent libnx, transport and the dedicated Yati boundary.
#include "nexus/release.hpp"
#include "nexus/update_transaction.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
using Result=unsigned;using u64=std::uint64_t;
constexpr Result NetworkFault=101,ProgramFault=102,StorageFault=103,SdFault=104,InstallerFault=105,Cancelled=106,FirmwareFault=107;
#define R_TRY(expr) do{const auto fixture_result=(expr);if(fixture_result)return fixture_result;}while(false)
#define R_UNLESS(expr,rc) do{if(!(expr))return (rc);}while(false)
#define R_SUCCEED() return 0
#define R_SUCCEEDED(rc) (!(rc))
template<class F>struct Scope{F f;~Scope(){f();}};
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__LINE__){[&]{__VA_ARGS__;}}
namespace nexus {constexpr Result Unavailable=55,Invalid=56;}
enum NcmStorageId{NcmStorageId_None,NcmStorageId_BuiltInSystem,NcmStorageId_BuiltInUser,NcmStorageId_SdCard,NcmStorageId_GameCard};
enum {AppletType_Application,AppletType_LibraryApplet,InfoType_ProgramId=18,CUR_PROCESS_HANDLE=42};
namespace mock {
std::string scenario,body;
bool native=true,network_active{},read_corrupted{};
unsigned program_queries{},storage_queries{},network_opens{},network_closes{},fetches{},commits{},renames{},opens{},closes{},installs{},sd_opens{},sd_checks{},transfers{};
std::vector<std::string> events;
NcmStorageId storage=NcmStorageId_SdCard;
nexus::updates::Release release;
const std::string root="sdmc:/switch/nexusmanager",folder=root+"/updates";
const std::string temp=folder+"/download-4242.part",current=folder+"/NexusManager-9.0.0-4242.nsp";
const std::string nro=root+"/NexusManager.nro",backup=folder+"/NexusManager-"+nexus::updates::Version+"-4242.nro.bak";
const std::string original="original NRO that must survive a refused update";
std::string Read(const std::string& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const std::string& path,const std::string& data){std::ofstream out(path,std::ios::binary);out.write(data.data(),data.size());assert(out.good());}
std::string Digest(std::string_view bytes){
    unsigned char digest[32];SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest);
    constexpr char hex[]="0123456789abcdef";std::string text;
    for(auto c:digest){text+=hex[c>>4];text+=hex[c&15];}return text;
}
FILE* Fopen(const char* path,const char* mode){
    ++opens;if((scenario=="stage-open"&&mode[0]=='w')||(scenario=="read-open"&&mode[0]=='r')){errno=EACCES;return nullptr;}
    return std::fopen(path,mode);
}
size_t Fwrite(const void* data,size_t size,size_t count,FILE* file){
    return std::fwrite(data,size,scenario=="short-write"?count-1:count,file);
}
int Fflush(FILE* file){const auto result=std::fflush(file);return scenario=="flush"?EOF:result;}
int Fsync(int fd){const auto result=::fsync(fd);return scenario=="sync"?-1:result;}
int Fclose(FILE* file){++closes;const auto result=std::fclose(file);return scenario=="close"&&closes==1?EOF:result;}
size_t Fread(void* data,size_t size,size_t count,FILE* file){
    if(scenario=="read-error")return 0;
    const auto got=std::fread(data,size,count,file);
    if(got&&scenario=="read-corrupt"&&!read_corrupted){static_cast<char*>(data)[0]^=1;read_corrupted=true;}
    return got;
}
int Ferror(FILE* file){return scenario=="read-error"?1:std::ferror(file);}
int Mkdir(const char* path,mode_t mode){if(scenario=="mkdir"){errno=EACCES;return -1;}return ::mkdir(path,mode);}
int Rename(const char* from,const char* to){
    ++renames;events.push_back("rename");
    if((scenario=="rename"&&renames==1)||(scenario=="nro-rename"&&renames==2)){errno=EIO;return -1;}
    return std::rename(from,to);
}
}
bool envIsNso(){return mock::native;}
int appletGetAppletType(){return mock::scenario=="wrong-applet"?AppletType_LibraryApplet:AppletType_Application;}
Result svcGetInfo(u64* value,int kind,int handle,int zero){
    assert(kind==InfoType_ProgramId&&handle==CUR_PROCESS_HANDLE&&zero==0);++mock::program_queries;
    *value=mock::scenario=="wrong-program"?1:0x05004e584d470000ULL;
    return mock::scenario=="program-query"?ProgramFault:0;
}
Result appletGetLaunchStorageInfoForDebug(NcmStorageId* storage,NcmStorageId* update_storage){
    ++mock::storage_queries;*storage=mock::storage;*update_storage=NcmStorageId_GameCard;
    return mock::scenario=="storage-query"?StorageFault:0;
}
int* fsdevGetDeviceFileSystem(const char* device){assert(std::string(device)=="sdmc:");static int fs;return mock::scenario=="sd-device"?nullptr:&fs;}
Result fsFsCommit(int* fs){
    assert(fs);++mock::commits;mock::events.push_back("commit");
    if((mock::scenario=="stage-commit"&&mock::commits==1)||
       (mock::scenario=="publish-commit"&&mock::commits==2)||
       (mock::scenario=="nro-backup-commit"&&mock::commits==2))return SdFault;
    return 0;
}
u64 armGetSystemTick(){return 4242;}
using Sha256Context=SHA256_CTX;
void sha256CalculateHash(void* digest,const void* data,size_t size){SHA256(static_cast<const unsigned char*>(data),size,static_cast<unsigned char*>(digest));}
void sha256ContextCreate(Sha256Context* context){assert(SHA256_Init(context)==1);}
void sha256ContextUpdate(Sha256Context* context,const void* data,size_t size){assert(SHA256_Update(context,data,size)==1);}
void sha256ContextGetHash(Sha256Context* context,void* digest){assert(SHA256_Final(static_cast<unsigned char*>(digest),context)==1);}
namespace sphaira {
struct App{
    static bool IsApplet(){return mock::scenario=="applet";}
    static const char* GetExePath(){return mock::scenario=="nro-path"?"/other.nro":"/switch/nexusmanager/NexusManager.nro";}
};
namespace ui {
struct ProgressBox{
    Result ShouldExitResult(){mock::events.push_back("cancel-check");return mock::scenario=="cancel"?Cancelled:0;}
    void NewTransfer(const char* title){assert(std::string(title)=="Installing Nexus Manager");++mock::transfers;}
};
}
}
namespace fs {
struct Fs{virtual ~Fs()=default;};
struct FsPath{std::string value;FsPath(const std::string& value):value(value){}};
struct FsNativeSd:Fs{
    FsNativeSd(){++mock::sd_opens;assert(!mock::network_active);}
    Result GetFsOpenResult(){++mock::sd_checks;return mock::scenario=="sd-open"?SdFault:0;}
};
}
namespace sphaira {
namespace yati {
Result CheckManagerUpdateStorage(NcmStorageId storage,std::string& detail){
    assert(storage==mock::storage&&!mock::network_active&&mock::network_opens==0);
    if(mock::scenario=="old-firmware"){
        detail="Update Nexus USB firmware before updating Manager on SD.";
        return FirmwareFault;
    }
    return 0;
}
Result InstallManagerUpdateFromLocalNsp(ui::ProgressBox* progress,fs::Fs* filesystem,const fs::FsPath& path,
    std::string_view version,std::string_view digest,u64 size,NcmStorageId storage,std::string& detail){
    assert(progress&&filesystem&&!mock::network_active&&mock::network_closes==1&&mock::sd_checks==1);
    assert(storage==mock::storage&&(storage==NcmStorageId_SdCard||storage==NcmStorageId_BuiltInUser));
    assert(version==mock::release.version&&digest==mock::release.sha256&&size==mock::release.size);
    assert(path.value==mock::current.substr(5));
    assert(mock::commits==2&&mock::renames==1&&mock::events.back()=="commit");
    const auto installed_bytes=mock::Read("sdmc:"+path.value);
    assert(installed_bytes==mock::body&&mock::Digest(installed_bytes)==digest&&installed_bytes.size()==size);
    assert(!std::filesystem::exists(mock::temp));
    ++mock::installs;mock::events.push_back("install");detail="Dedicated installer rejected update";
    return mock::scenario=="installer"?InstallerFault:0;
}
}
}
namespace nexus::updates {
using sphaira::ui::ProgressBox;
struct Network{
    bool active{};
    Result Open(){++mock::network_opens;if(mock::scenario=="network")return NetworkFault;active=mock::network_active=true;return 0;}
    ~Network(){if(active){mock::network_active=false;++mock::network_closes;mock::events.push_back("network-close");}}
};
Result Fetch(ProgressBox* progress,const std::string& url,size_t limit,std::vector<char>& bytes,std::string& detail){
    assert(progress&&mock::network_active&&url==mock::release.url&&limit==mock::release.size);++mock::fetches;
    if(mock::scenario=="fetch"){detail="Mock transport failure";return NetworkFault;}
    bytes.assign(mock::body.begin(),mock::body.end());
    if(mock::scenario=="download-corrupt")bytes.back()^=1;
    return 0;
}
// INSTALL_HEX_PRODUCTION
#define fopen(...) mock::Fopen(__VA_ARGS__)
#define fwrite(...) mock::Fwrite(__VA_ARGS__)
#define fflush(...) mock::Fflush(__VA_ARGS__)
#define fsync(...) mock::Fsync(__VA_ARGS__)
#define fclose(...) mock::Fclose(__VA_ARGS__)
#define fread(...) mock::Fread(__VA_ARGS__)
#define ferror(...) mock::Ferror(__VA_ARGS__)
#define mkdir(...) mock::Mkdir(__VA_ARGS__)
#define rename(...) mock::Rename(__VA_ARGS__)
// INSTALL_FUNCTION_PRODUCTION
#undef fopen
#undef fwrite
#undef fflush
#undef fsync
#undef fclose
#undef fread
#undef ferror
#undef mkdir
#undef rename
}

void Set(std::string& data,size_t at,u64 value,unsigned size){for(unsigned i=0;i<size;++i)data[at+i]=char(value>>(i*8));}
std::string Payload(bool native){
    std::string data(65536,'\0');
    if(native){
        std::string names;std::vector<size_t> offsets;
        for(auto name:{std::string(32,'a')+".nca",std::string(32,'b')+".nca",std::string(32,'c')+".cnmt.nca"}){
            offsets.push_back(names.size());names+=name;names+='\0';
        }
        data.replace(0,4,"PFS0");Set(data,4,3,4);Set(data,8,names.size(),4);data.replace(88,names.size(),names);
        u64 position=0;for(unsigned i=0;i<3;++i){const u64 size=i==2?data.size()-88-names.size()-position:4096;
            Set(data,16+i*24,position,8);Set(data,24+i*24,size,8);Set(data,32+i*24,offsets[i],4);position+=size;}
        assert(nexus::updates::ValidNsp(data));
    }else{
        data.replace(16,4,"NRO0");Set(data,24,128,4);data.replace(128,4,"ASET");
        Set(data,136,56,8);Set(data,144,8,8);Set(data,152,64,8);Set(data,160,0x4000,8);
        Set(data,168,64+0x4000,8);Set(data,176,32,8);
        data.replace(192,13,"Nexus Manager");data.replace(192+0x3060,5,"9.0.0");
        assert(nexus::updates::ValidNro(data,"9.0.0"));
    }
    return data;
}
int main(int argc,char** argv){
    assert(argc==2);using namespace mock;using namespace nexus::updates;
    scenario=argv[1];native=!scenario.starts_with("nro");
    if(scenario=="internal")storage=NcmStorageId_BuiltInUser;
    if(scenario=="storage-none")storage=NcmStorageId_None;
    if(scenario=="storage-system")storage=NcmStorageId_BuiltInSystem;
    if(scenario=="storage-gamecard")storage=NcmStorageId_GameCard;
    body=Payload(native);if(scenario=="invalid-payload")body[0]='X';
    release.kind=native?Artifact::Nsp:Artifact::Nro;release.version="9.0.0";release.tag="v9.0.0";
    release.url=std::string(Repository)+"/releases/download/"+release.tag+"/"+AssetName(release.kind);
    release.size=body.size();release.sha256=Digest(body);
    if(scenario=="wrong-kind")release.kind=Artifact::Nro;
    if(scenario=="current-version"){release.version=Version;release.tag=std::string("v")+Version;release.url=std::string(Repository)+"/releases/download/"+release.tag+"/NexusManager.nsp";}
    if(scenario=="wrong-owner")release.url="https://example.invalid/NexusManager.nsp";
    if(scenario=="wrong-size")++release.size;
    std::filesystem::create_directories(root);Write(nro,original);
    if(scenario=="existing-dir"||scenario=="existing-part"||scenario=="existing-final"||scenario=="nro-backup")std::filesystem::create_directory(folder);
    if(scenario=="folder-file")Write(folder,"not a directory");
    if(scenario=="existing-part")Write(temp,"unowned partial file");
    if(scenario=="existing-final")Write(current,"unowned final file");
    if(scenario=="nro-backup")Write(backup,"previous backup");
    if(scenario=="nro-missing")std::filesystem::remove(nro);
    sphaira::ui::ProgressBox progress;std::string detail;
    const auto result=Install(&progress,release,detail);
    const bool success=scenario=="sd"||scenario=="internal"||scenario=="existing-dir"||scenario=="nro";
    if((result==0)!=success)std::cerr<<scenario<<" unexpected result "<<result<<" detail="<<detail<<"\n";
    assert((result==0)==success&&!network_active);
    assert(installs==(native&&(success||scenario=="installer")?1u:0u));
    assert(!std::filesystem::exists(temp)||scenario=="existing-part");
    if(success)assert(detail=="Update installed. Restarting Nexus Manager.");
    else assert(detail!="Update installed. Restarting Nexus Manager.");
    if(scenario=="installer")assert(result==InstallerFault&&detail=="Dedicated installer rejected update");
    if(scenario=="sd-open")assert(result==SdFault);
    if(scenario=="cancel")assert(result==Cancelled&&renames==0);
    if(scenario=="network"||scenario=="fetch")assert(result==NetworkFault&&opens==0);
    if(scenario=="program-query")assert(result==ProgramFault&&storage_queries==0&&network_opens==0);
    if(scenario=="storage-query")assert(result==StorageFault&&network_opens==0);
    if(scenario=="old-firmware")assert(result==FirmwareFault&&network_opens==0&&fetches==0&&installs==0&&opens==0);
    if(scenario=="applet"||scenario=="wrong-kind"||scenario=="current-version"||scenario=="wrong-owner"||
       scenario=="wrong-applet"||scenario=="wrong-program"||scenario.starts_with("storage-")||
       scenario=="nro-path"||scenario=="nro-missing")assert(network_opens==0&&fetches==0&&installs==0);
    if(scenario=="download-corrupt"||scenario=="wrong-size"||scenario=="invalid-payload")assert(opens==0&&renames==0);
    if(native)assert(Read(nro)==original);
    else if(success)assert(Read(nro)==body&&Read(backup)==original&&sd_opens==0&&installs==0);
    else if(scenario!="nro-missing")assert(Read(nro)==original);
    if(scenario=="existing-part")assert(Read(temp)=="unowned partial file");
    if(scenario=="existing-final")assert(Read(current)=="unowned final file");
    if(scenario=="nro-backup")assert(Read(backup)=="previous backup");
    std::cout<<scenario<<" passed\n";
}
