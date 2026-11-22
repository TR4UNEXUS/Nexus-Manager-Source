#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/homebrew.hpp"
#include "nexus/vsd_policy.hpp"
#include "nexus/library.hpp"
#include "nacp_compat.hpp"
#include "owo.hpp"
#include "fs.hpp"
#include "image.hpp"
#include "nro.hpp"
#include <set>
#include <algorithm>
#include <array>
#include <cstring>

namespace sphaira::nexusapp {
namespace {
#if NEXUS_BUNDLED_PROBE
alignas(16) const unsigned char Probe[]={
    #embed <VsdProbe.nro>
};
#endif
constexpr Result Missing=MAKERESULT(140,205);
Result ReadExact(nexus::Client& c,u64 gen,const std::string& path,u64 off,void* out,size_t size){
    u64 got{};R_TRY(c.Read(gen,path,off,out,size,got));R_UNLESS(got==size,nexus::Invalid);R_SUCCEED();
}
}
Result ReadHomebrew(const Item& item,u64 gen,Game& game){
    game={};nexus_vsd::Profile profile{};
    R_UNLESS(!item.directory && nexus_vsd::Parse(item.path,profile),nexus::Invalid);
    nexus::Client c;R_TRY(c.Check(gen));nexus::Info info{};R_TRY(c.Stat(gen,item.path,info));
    R_UNLESS(!info.directory && info.size==item.size,nexus::Invalid);
    std::array<u8,0x80> header{};R_TRY(ReadExact(c,gen,item.path,0,header.data(),header.size()));
    nexus::library::Assets a{};R_UNLESS(nexus::library::Header(header,info.size,a),nexus::Invalid);
    game.name=item.name;game.homebrew_path=item.path;game.generation=gen;game.file_size=info.size;
    std::array<u8,56> assets{};
    if(nexus::library::Range(a.code,assets.size(),info.size)){
        R_TRY(ReadExact(c,gen,item.path,a.code,assets.data(),assets.size()));
        if(nexus::library::AssetHeader(assets,info.size,a)){
            game.icon_offset=a.icon;game.icon_size=a.icon_size;
            if(a.nacp_size){
                NacpStruct nacp{};R_TRY(ReadExact(c,gen,item.path,a.nacp,&nacp,sizeof(nacp)));
                for(const auto& lang:NacpLanguageEntries(nacp))if(lang.name[0] && std::memchr(lang.name,0,sizeof(lang.name))){game.name=lang.name;break;}
            }
        }
    }
    return c.Check(gen);
}
Result ReadHomebrewIcon(const Game& game,std::vector<u8>& icon){
    icon.clear();R_UNLESS(game.icon_size && game.icon_size<=1024*1024 && nexus::library::Range(game.icon_offset,game.icon_size,game.file_size),nexus::Invalid);
    nexus::Client c;R_TRY(c.Check(game.generation));nexus::Info info{};R_TRY(c.Stat(game.generation,game.homebrew_path,info));
    R_UNLESS(!info.directory && info.size==game.file_size,nexus::Invalid);
    icon.resize(game.icon_size);R_TRY(ReadExact(c,game.generation,game.homebrew_path,game.icon_offset,icon.data(),icon.size()));
    return c.Check(game.generation);
}
Result ScanHomebrew(ui::ProgressBox* p,std::vector<Game>& games){
    nexus::Client c;nexus::Status status{};
    const auto probe=c.Query(status);
    if(R_FAILED(probe) || !status.Available())R_SUCCEED(); // installed library works without USB
    nexus::Info root{};const auto exists=c.Stat(status.generation,"/switch",root);
    if(exists==Missing)R_SUCCEED();R_TRY(exists);R_UNLESS(root.directory,nexus::Invalid);
    std::vector<std::pair<std::string,unsigned>> pending{{"/switch",0}};
    std::set<std::string> visited;size_t entries=0,found=0;
    // Keep a failed/unplugged scan out of the visible library entirely.
    std::vector<Game> result;
    for(size_t at=0;at<pending.size();++at){
        R_TRY(p->ShouldExitResult());R_TRY(c.Check(status.generation));
        const auto [folder,depth]=pending[at];
        if(!visited.insert(folder).second)continue;
        std::vector<nexus::Entry> list;R_TRY(c.List(status.generation,folder,list));
        entries+=list.size();R_UNLESS(entries<=65536,nexus::Invalid);
        for(const auto& e:list){
            R_TRY(p->ShouldExitResult());const auto path=folder+"/"+e.name;
            if(!nexus_vsd::Canonical(path) || path.size()>=256)continue;
            if(e.directory){
                R_UNLESS(depth<32 && pending.size()<4096,nexus::Invalid);
                pending.emplace_back(path,depth+1);
            }else if(nexus_vsd::Nro(path)){
                R_UNLESS(++found<=4096,nexus::Invalid);
                Game game;const auto rc=ReadHomebrew({e.name,path,e.size,false},status.generation,game);
                if(R_SUCCEEDED(rc))result.push_back(std::move(game));
                else {R_TRY(c.Check(status.generation));nexus::Log("library-invalid-nro",rc);}
            }
        }
        p->NewTransfer("Scanning USB /switch"_nx);p->UpdateTransfer(at+1,pending.size());
    }
    R_TRY(c.Check(status.generation));
    std::ranges::sort(result,[](const Game& a,const Game& b){return strcasecmp(a.name.c_str(),b.name.c_str())<0;});
    games.insert(games.end(),std::make_move_iterator(result.begin()),std::make_move_iterator(result.end()));R_SUCCEED();
}
Result CreateHomebrewForwarder(ui::ProgressBox* p,const Item& item,u64 gen){
    Game game;R_TRY(ReadHomebrew(item,gen,game));
    nexus::Client c;R_TRY(c.Check(gen));NexusVsdStatus status{};R_TRY(c.VirtualSdStatus(status));
    nexus_vsd::Profile profile{};R_UNLESS(nexus_vsd::Parse(item.path,profile),nexus::Invalid);
    R_UNLESS(!nexus_vsd::UsbRoot(profile) || (status.reserved&NEXUS_VSD_CAP_USB_ROOT),nexus::Unavailable);
    OwoConfig cfg{};cfg.nro_path=std::string("sdmc:")+profile.logical;cfg.usb_path=item.path;
    cfg.name=game.name;cfg.author="Nexus Manager / Homebrew";cfg.address_space=ForwarderAddressSpace::Bit39;
    cfg.core_mode=CpuCoreMode::Four;
    cfg.nacp.supported_language_flag=0xffff;
    cfg.nacp.accessible_program_index_flags=1;
    cfg.nacp.initial_program_index=0;
    cfg.nacp.program_index=0;
    std::strcpy(cfg.nacp.display_version,"1.0.0");
    if(R_SUCCEEDED(ReadHomebrewIcon(game,cfg.icon)))cfg.icon=ImageNormalizeIcon(cfg.icon);
    if(cfg.icon.empty()){
        fs::FsStdio fs;R_TRY(fs.read_entire_file("romfs:/usb-loader-icon.jpg",cfg.icon));
    }
    // Bind executable bytes, not save/data files. Recreate after updating the NRO.
    std::array<u8,0x80> header{};R_TRY(ReadExact(c,gen,item.path,0,header.data(),header.size()));
    nexus::library::Assets a{};R_UNLESS(nexus::library::Header(header,item.size,a),nexus::Invalid);
    Sha256Context hash;sha256ContextCreate(&hash);std::vector<u8> buffer(1024*1024);
    p->NewTransfer("Verify homebrew executable"_nx);
    for(u64 off=0;off<a.code;){R_TRY(p->ShouldExitResult());const auto size=std::min<u64>(buffer.size(),a.code-off);
        R_TRY(ReadExact(c,gen,item.path,off,buffer.data(),size));sha256ContextUpdate(&hash,buffer.data(),size);off+=size;p->UpdateTransfer(off,a.code);}
    cfg.usb_nro_hash.resize(32);sha256ContextGetHash(&hash,cfg.usb_nro_hash.data());R_TRY(c.Check(gen));
    return install_forwarder(p,cfg,NcmStorageId_BuiltInUser);
}
Result ListHomebrew(const std::string& path,std::vector<Item>& items,u64& generation,bool usb) {
    items.clear();
    // Directory-at-a-time browsing: no recursive scan of large game assets.
    if(path!="/" && path!="/switch" && path!="/homebrew" && !path.starts_with("/switch/") && !path.starts_with("/homebrew/"))return nexus::Invalid;
    R_TRY(ListFiles(usb,path,items,generation));
    std::erase_if(items,[&](const Item& item){
        if(path=="/")return !item.directory || (item.path!="/switch" && item.path!="/homebrew");
        return !item.directory && !nexus_vsd::Nro(item.path);
    });
    R_SUCCEED();
}
Result PrepareProbe(ui::ProgressBox* progress,u64 generation,Item& item) {
    item={};
#if !NEXUS_BUNDLED_PROBE
    (void)progress;(void)generation;return nexus::Unavailable;
#else
    nexus::Client client;R_TRY(client.Check(generation));
    nexus::Info info{};
    auto rc=client.Stat(generation,"/homebrew",info);
    if(rc==Missing){R_TRY(client.CreateDirectory(generation,"/homebrew"));}
    else {R_TRY(rc);R_UNLESS(info.directory,nexus::Invalid);}
    // Create an unpredictable NEW directory before using the provider's
    // create/truncate file command. Never truncate an existing user profile.
    std::array<u8,16> nonce{};randomGet(nonce.data(),nonce.size());
    const auto root="/homebrew/usbloader-probe-"+HexId(nonce.data(),nonce.size());
    R_TRY(progress->ShouldExitResult());
    R_TRY(client.CreateDirectory(generation,root)); // existing directory fails
    R_TRY(client.CreateDirectory(generation,root+"/sdmc"));
    R_TRY(client.CreateDirectory(generation,root+"/sdmc/switch"));
    const auto file=root+"/sdmc/switch/VsdProbe.nro";
    R_TRY(client.CreateFile(generation,file,sizeof(Probe)));
    progress->NewTransfer("Preparing built-in USB diagnostics"_nx);
    for(size_t off=0;off<sizeof(Probe);){
        R_TRY(progress->ShouldExitResult());
        const auto count=std::min<size_t>(65536,sizeof(Probe)-off);
        R_TRY(client.Write(generation,file,off,Probe+off,count));
        off+=count;progress->UpdateTransfer(off,sizeof(Probe));
    }
    // Verify every byte, not only size/header, before offering the handoff.
    std::vector<u8> check(65536);
    for(size_t off=0;off<sizeof(Probe);){
        R_TRY(progress->ShouldExitResult());
        const auto count=std::min(check.size(),sizeof(Probe)-off);u64 got{};
        R_TRY(client.Read(generation,file,off,check.data(),count,got));
        R_UNLESS(got==count && !std::memcmp(check.data(),Probe+off,count),nexus::Invalid);
        off+=count;
    }
    R_TRY(client.Stat(generation,file,info));
    R_UNLESS(!info.directory && info.size==sizeof(Probe),nexus::Invalid);
    R_TRY(client.Check(generation));
    item={"USB diagnostics (built-in)"_nx,file,sizeof(Probe),false};
    R_SUCCEED();
#endif
}
}
