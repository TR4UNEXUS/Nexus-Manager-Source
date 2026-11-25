// SPDX-License-Identifier: GPL-3.0-or-later
// Uses the Nexus HATS Installer's shared protection policy and installer payload.
#include "nexus/hats.hpp"
#include "nexus/hats_core.hpp"
#include "nexus/hats_shared/hats_contents_policy.hpp"
#include "../../../extras/shared/nexus_boot_profile.h"
#include "nexus/update.hpp"
#include "nexus/localize.hpp"
#include "nexus/problem_reports.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include <minIni.h>
#include <minizip/unzip.h>
#include <set>
#include <cstdio>
#include <cstring>

namespace nexus::updates {
Result FetchHatsData(sphaira::ui::ProgressBox*,const std::string&,size_t,std::vector<char>&,std::string&);
}
namespace nexus::hats {
using sphaira::App;
using namespace sphaira::ui;
using namespace sphaira::ui::menu::hats;
namespace {
constexpr const char* Stage="/config/nexus-hats-installer/hats-staging";
constexpr const char* Zip="/config/nexusmanager/hats-download.zip";
constexpr const char* Payload="/switch/nexus-hats-installer/hats-installer.bin";
constexpr const char* Boot="/bootloader/hekate_ipl.ini";
constexpr const char* BootBackup="/config/nexus-hats-installer/hekate_ipl.ini.bak";
constexpr const char* Config="sdmc:/config/nexusmanager/hats.ini";
constexpr Result Bad=MAKERESULT(Module_Libnx,LibnxError_BadInput);
constexpr Result Io=MAKERESULT(Module_Libnx,LibnxError_IoError);
constexpr size_t MaxZip=128*1024*1024;
std::string ReadText(const char* name,size_t limit=65536) {
    FILE* f=fopen(name,"rb");if(!f)return {};std::string text(limit+1,'\0');
    size_t n=fread(text.data(),1,text.size(),f);bool valid=!ferror(f)&&n<=limit;fclose(f);
    if(!valid||text.find('\0',0)<n)return {};text.resize(n);return text;
}
Result Save(fs::FsNativeSd& sd,const char* path,std::span<const u8> bytes) {
    R_TRY(sd.CreateDirectoryRecursivelyWithPath(path));
    return sd.write_entire_file(path,bytes);
}
Result SaveText(fs::FsNativeSd& sd,const char* path,const std::string& text) {
    return Save(sd,path,{reinterpret_cast<const u8*>(text.data()),text.size()});
}
Result CopyBundled(fs::FsNativeSd& sd,const char* name,const char* destination) {
    std::string path=std::string("romfs:/nexus-hats/")+name;
    FILE* f=fopen(path.c_str(),"rb");R_UNLESS(f,Io);ON_SCOPE_EXIT(fclose(f));
    R_UNLESS(!fseek(f,0,SEEK_END),Io);long length=ftell(f);R_UNLESS(length>0&&length<1024*1024,Io);rewind(f);
    std::vector<u8> bytes(length);R_UNLESS(fread(bytes.data(),1,bytes.size(),f)==bytes.size(),Io);
    return Save(sd,destination,bytes);
}
std::string Hash(std::span<const u8> bytes) {
    u8 digest[32];sha256CalculateHash(digest,bytes.data(),bytes.size());std::string out;
    constexpr char hex[]="0123456789abcdef";for(u8 c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
Result ValidateArchive(u64& total) {
    zlib_filefunc64_def funcs;sphaira::mz::FileFuncStdio(&funcs);
    auto* z=unzOpen2_64(Zip,&funcs);R_UNLESS(z,Bad);ON_SCOPE_EXIT(unzClose(z));
    unz_global_info64 global{};R_UNLESS(unzGetGlobalInfo64(z,&global)==UNZ_OK&&global.number_entry&&global.number_entry<20000,Bad);
    R_UNLESS(unzGoToFirstFile(z)==UNZ_OK,Bad);std::set<std::string> paths;bool core{},loader{},boot{};total=0;
    for(u64 i=0;i<global.number_entry;++i){
        if(i)R_UNLESS(unzGoToNextFile(z)==UNZ_OK,Bad);
        unz_file_info64 info{};char name[256]{};
        R_UNLESS(unzGetCurrentFileInfo64(z,&info,name,sizeof(name),nullptr,0,nullptr,0)==UNZ_OK,Bad);
        R_UNLESS(info.size_filename&&info.size_filename<sizeof(name)&&strlen(name)==info.size_filename&&core::Path(name),Bad);
        const auto path=core::Lower(name);
        R_UNLESS(paths.insert(path).second&&!(info.flag&1)&&((info.external_fa>>16)&0170000)!=0120000,Bad);
        R_UNLESS(!path.starts_with("config/nexus-hats-installer/")&&!path.starts_with("config/nexusmanager/"),Bad);
        R_UNLESS(info.uncompressed_size<=512ULL*1024*1024&&total<=1024ULL*1024*1024-info.uncompressed_size,Bad);total+=info.uncompressed_size;
        if(path=="atmosphere/package3")core=info.uncompressed_size>1024;
        if(path=="atmosphere/kips/nexus-loader.kip")loader=info.uncompressed_size>1024;
        if(path=="bootloader/hekate_ipl.ini")boot=info.uncompressed_size>0;
    }
    R_UNLESS(core&&loader&&boot,Bad);return 0;
}
Result Extract(ProgressBox* p,fs::FsNativeSd& sd,u64 total) {
    zlib_filefunc64_def funcs;sphaira::mz::FileFuncStdio(&funcs);
    auto* z=unzOpen2_64(Zip,&funcs);R_UNLESS(z,Bad);ON_SCOPE_EXIT(unzClose(z));
    unz_global_info64 global{};R_UNLESS(unzGetGlobalInfo64(z,&global)==UNZ_OK,Bad);
    R_UNLESS(unzGoToFirstFile(z)==UNZ_OK,Bad);u64 done=0;
    for(u64 i=0;i<global.number_entry;++i){
        R_TRY(p->ShouldExitResult());if(i)R_UNLESS(unzGoToNextFile(z)==UNZ_OK,Bad);
        unz_file_info64 info{};char name[256]{};
        R_UNLESS(unzGetCurrentFileInfo64(z,&info,name,sizeof(name),nullptr,0,nullptr,0)==UNZ_OK&&core::Path(name),Bad);
        const auto path=std::string(Stage)+"/"+name;
        if(path.back()=='/'){R_TRY(sd.CreateDirectoryRecursively(path.c_str()));continue;}
        R_UNLESS(unzOpenCurrentFile(z)==UNZ_OK,Bad);
        Result result{};
        {ProgressScope file(p,nexus::WorkPart(done,total),nexus::WorkPart(done+info.uncompressed_size,total));
            result=sphaira::thread::TransferUnzip(p,z,&sd,path.c_str(),info.uncompressed_size,info.crc);}
        const auto closed=unzCloseCurrentFile(z);R_TRY(result);R_UNLESS(closed==UNZ_OK,Bad);done+=info.uncompressed_size;
    }
    R_UNLESS(done==total,Bad);return 0;
}
Result PreparePolicy(ProgressBox* p,fs::FsNativeSd& sd,std::string& detail) {
    auto policy=std::make_unique<mm_contents_policy>();
    for(const auto& name:{std::string("/manifest.json"),std::string(Stage)+"/manifest.json"}){
        const auto text=ReadText(name.c_str(),2*1024*1024);
        if(text.empty())continue;
        auto* doc=yyjson_read(text.data(),text.size(),0);R_UNLESS(doc,Bad);
        AddManifestSysmodules(*policy,yyjson_doc_get_root(doc));yyjson_doc_free(doc);
    }
    std::vector<char> exclusions;
    // Always include bundled protections. Remote rules may only add protections.
    const auto bundled=ReadText("romfs:/nexus-hats/nexus-exclusions.json",HATS_EXCLUSIONS_MAX_JSON);
    R_UNLESS(ParseGithubExclusions(*policy,bundled.data(),bundled.size()),Bad);
    auto rc=updates::FetchHatsData(p,HATS_EXCLUSIONS_URL,HATS_EXCLUSIONS_MAX_JSON,exclusions,detail);
    // Network failures stop preparation; an unavailable list is never permission
    // to replace game data. The shipped list also remains in the payload.
    R_TRY(rc);auto remote=std::make_unique<mm_contents_policy>(*policy);
    R_UNLESS(ParseGithubExclusions(*remote,exclusions.data(),exclusions.size()),Bad);
    for(unsigned pos=0;pos<remote->preserve_size;){const char* path=remote->preserve_paths+pos;
        R_UNLESS(mm_policy_preserve(policy.get(),path,strlen(path)),Bad);pos+=strlen(path)+1;}
    for(const char* path:{"Nintendo","emuMMC","switch/nexusmanager"})
        R_UNLESS(mm_policy_preserve(policy.get(),path,strlen(path)),Bad);
    R_TRY(SaveText(sd,MM_CONTENTS_POLICY_PATH,SerializeContentsPolicy(*policy)));return sd.Commit();
}
void ShowPacks(const Release& release) {
    Identity identity;ReadIdentity(ReadText(IdentityPath,4096),identity);
    const auto edition=identity.edition=="standard-no-hoc"?"Standard No-HOC"_nx:
        identity.edition=="standard-hoc"?"Standard HOC"_nx:
        identity.edition=="nexus-themed-hoc"?"Nexus-themed HOC"_nx:std::string{};
    const auto heading=release.installed.empty()?"Installed HATS version could not be identified. Choose a pack to update or reinstall."_nx:
        "Installed: "_nx+ReleaseLabel(release.installed)+(edition.empty()?"":" - "+edition)+"    Available: "_nx+ReleaseLabel(release.tag);
    auto menu=std::make_unique<Sidebar>("Nexus HATS updates"_nx,heading,Sidebar::Side::RIGHT,1280.f);
    for(const auto& pack:release.packs){
        char size[48];snprintf(size,sizeof(size),"%.1f MiB",pack.size/1048576.0);
        auto lower=core::Lower(pack.asset_name);
        const std::string description=lower.find("no-hoc")!=lower.npos?"Standard appearance, without HOC."_nx:
            lower.find("themed")!=lower.npos?"Nexus theme and boot icons, with HOC."_nx:"Standard appearance, with HOC."_nx;
        menu->Add<SidebarEntryCallback>(PackDisplayName(pack)+"  |  "+size,[pack,description]{
            App::Push<OptionBox>(PackDisplayName(pack)+"\n\n"+description+"\n\n"+
                "Download and prepare this HATS pack? Your games, saves, USB maps and protected settings are kept. Finish other tasks and close any running game first. Installation requires a restart; keep the SD card inserted and connect the charger."_nx,
                "Cancel"_nx,"Download"_nx,0,[pack](auto choice){
                    if(!choice||*choice!=1)return;auto detail=std::make_shared<std::string>();
                    App::Push<ProgressBox>(0,"Nexus HATS update"_nx,PackDisplayName(pack),[pack,detail](ProgressBox* p){return Prepare(p,pack,*detail);},
                        [detail](Result rc){if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"HATS preparation stopped. The installed firmware was not changed."_nx:*detail);return;}
                            App::Push<OptionBox>("HATS is ready. Restart into the protected installer now? Keep the charger connected and do not remove the SD card. You can choose Later and prepare the pack again when ready."_nx,
                                "Later"_nx,"Install and restart"_nx,0,[](auto choice){if(!choice||*choice!=1)return;auto error=std::make_shared<std::string>();
                                    App::Push<ProgressBox>(0,"Starting HATS installer"_nx,"Saving the boot configuration"_nx,[error](ProgressBox*){return Reboot(*error);},
                                        [error](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*error);});});});
                });
        },description);
    }
    App::Push(std::move(menu));
}
}
std::string Installed() {
    const auto text=ReadText(IdentityPath,4096);Identity identity;
    if(!text.empty())return ReadIdentity(text,identity)?identity.tag:std::string{};
    return core::InstalledTag(ReadText("/licenses/Components.txt"));
}
bool SupportsPcLibrary() {
    Identity identity;
    return ReadIdentity(ReadText(IdentityPath,4096),identity)&&identity.sequence==1;
}
Result Prepare(ProgressBox* p,const Pack& pack,std::string& detail) {
    R_UNLESS(ValidPack(pack)&&!App::IsApplet(),Bad);
    fs::FsNativeSd sd;R_TRY(sd.GetFsOpenResult());
    if(sd.FileExists(BootBackup)){detail="An earlier HATS installation still has a boot recovery file. Open Nexus HATS Installer to finish or recover it before starting another update."_nx;return Bad;}
    if(sd.FileExists(MM_CONTENTS_POLICY_PATH))R_TRY(sd.DeleteFile(MM_CONTENTS_POLICY_PATH));
    R_TRY(sd.Commit());
    s64 free{};R_TRY(sd.GetFreeSpace("/",&free));R_UNLESS(free>0&&u64(free)>pack.size*2+64*1024*1024,MAKERESULT(2,30));
    std::vector<char> data;
    {ProgressScope range(p,0,60);p->NewTransfer("Downloading HATS"_nx);R_TRY(updates::FetchHatsData(p,pack.download_url,pack.size,data,detail));}
    R_UNLESS(data.size()==pack.size&&"sha256:"+Hash({reinterpret_cast<const u8*>(data.data()),data.size()})==pack.asset_digest,Bad);
    R_TRY(p->ShouldExitResult());R_TRY(Save(sd,Zip,{reinterpret_cast<const u8*>(data.data()),data.size()}));data.clear();data.shrink_to_fit();
    ON_SCOPE_EXIT(sd.DeleteFile(Zip));
    u64 unpacked{};R_TRY(ValidateArchive(unpacked));R_TRY(sd.GetFreeSpace("/",&free));
    R_UNLESS(free>0&&u64(free)>unpacked+16*1024*1024,MAKERESULT(2,30));
    if(sd.DirExists(Stage))R_TRY(sd.DeleteDirectoryRecursively(Stage));R_TRY(sd.CreateDirectoryRecursively(Stage));
    bool prepared=false;ON_SCOPE_EXIT(if(!prepared){sd.DeleteFile(MM_CONTENTS_POLICY_PATH);sd.DeleteDirectoryRecursively(Stage);sd.Commit();});
    {ProgressScope range(p,60,90);p->NewTransfer("Preparing HATS files"_nx);
        R_TRY(Extract(p,sd,unpacked));}
    if(Sequence(pack.tag_name)){
        Identity identity;
        R_UNLESS(ReadIdentity(ReadText((std::string(Stage)+IdentityPath).c_str(),4096),identity)&&MatchesPack(identity,pack.tag_name,pack.asset_name),Bad);
    }
    {
        const auto original=ReadText(Boot),incoming=ReadText((std::string(Stage)+Boot).c_str());
        std::vector<char> checked(original.size()*3+16384);size_t used{};unsigned entries{};
        if(!nexus_merge_boot_profile(original.data(),original.size(),incoming.data(),incoming.size(),checked.data(),checked.size(),&used,&entries)){
            detail="This boot configuration needs a manual update. No firmware was changed. Copy the HATS pack on a PC and use its Hekate Launch entries."_nx;return Bad;
        }
    }
    R_TRY(romfsInit());ON_SCOPE_EXIT(romfsExit());
    p->SetOverallTransfer(90,100,"Protecting games and USB maps"_nx,true);
    R_TRY(CopyBundled(sd,"hats-installer.bin",Payload));
    std::vector<u8> payload;R_TRY(sd.read_entire_file(Payload,payload));R_UNLESS(HasModProtection(payload),Bad);
    R_TRY(PreparePolicy(p,sd,detail));R_TRY(p->ShouldExitResult());R_TRY(sd.Commit());
    prepared=true;p->SetOverallTransfer(100,100,"Ready to install"_nx,true);return 0;
}
Result Reboot(std::string& detail) {
    fs::FsNativeSd sd;R_TRY(sd.GetFsOpenResult());
    detail="The installer could not restart. Your original boot configuration was kept. Reopen HATS updates and try again."_nx;
    R_UNLESS(!sd.FileExists(BootBackup)&&sd.FileExists(MM_CONTENTS_POLICY_PATH)&&sd.FileExists(Payload)&&sd.FileExists((std::string(Stage)+"/atmosphere/package3").c_str()),Bad);
    const auto original=ReadText(Boot);R_UNLESS(!original.empty()&&original.find("[HATS Installer]")==original.npos,Bad);
    const auto pending=std::string(BootBackup)+".pending";
    ON_SCOPE_EXIT(sd.DeleteFile(pending.c_str()));
    R_TRY(SaveText(sd,pending.c_str(),original));R_TRY(sd.Commit());
    R_UNLESS(ReadText(pending.c_str())==original,Io);
    R_TRY(sd.RenameFile(pending.c_str(),BootBackup));R_TRY(sd.Commit());
    R_UNLESS(ReadText(BootBackup)==original,Io);
    bool requested=false;ON_SCOPE_EXIT(if(!requested){
        if(R_SUCCEEDED(SaveText(sd,Boot,original))&&R_SUCCEEDED(sd.Commit())&&ReadText(Boot)==original){sd.DeleteFile(BootBackup);sd.Commit();}
        else detail="The SD card could not restore the boot configuration. Do not restart yet. Restore bootloader/hekate_ipl.ini from config/nexus-hats-installer/hekate_ipl.ini.bak on a PC."_nx;
    });
    R_TRY(romfsInit());ON_SCOPE_EXIT(romfsExit());
    const auto temporary=ReadText("romfs:/nexus-hats/hekate_ipl_mod.ini");R_UNLESS(!temporary.empty(),Bad);
    R_TRY(SaveText(sd,Boot,temporary));R_TRY(sd.Commit());R_UNLESS(ReadText(Boot)==temporary,Io);
    auto rc=appletRequestToReboot();
    if(R_FAILED(rc)){rc=spsmInitialize();if(R_SUCCEEDED(rc)){rc=spsmShutdown(true);spsmExit();}}
    if(R_FAILED(rc)){rc=bpcInitialize();if(R_SUCCEEDED(rc)){rc=bpcRebootSystem();bpcExit();}}
    requested=R_SUCCEEDED(rc);return rc;
}
void Display() {
    reports::Stop();updates::StopStartupCheck();auto result=std::make_shared<Release>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Checking Nexus HATS"_nx,"Finding the latest packs"_nx,[result,detail](ProgressBox* p){return Check(p,*result,*detail);},
        [result,detail](Result rc){if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"Could not check HATS updates. Check the network connection and try again."_nx:*detail);return;}ShowPacks(*result);},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Prompt(const Release& release) {
    char dismissed[100]{};ini_gets("updates","dismissed","",dismissed,sizeof(dismissed),Config);
    if(!release.newer||release.tag==dismissed)return;
    App::Push<OptionBox>("A new Nexus HATS pack is available."_nx+std::string("\n\n")+ReleaseLabel(release.installed)+" -> "+ReleaseLabel(release.tag)+"\n\n"+
        "Manager updates and HATS updates are separate. Open HATS updates to choose a pack and install the latest USB firmware."_nx,
        "Not now"_nx,"View packs"_nx,0,[release](auto choice){
            if(choice&&*choice==1){reports::Stop();ShowPacks(release);return;}
            fs::FsNativeSd sd;if(R_SUCCEEDED(sd.CreateDirectoryRecursively("/config/nexusmanager")))ini_puts("updates","dismissed",release.tag.c_str(),Config);
        });
}
}
