// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer_exports.hpp"
#include "nexus/transfer.hpp"
#include "nexus/dump_stream.hpp"
#include "nexus/manager.hpp"
#include "nexus/lifecycle.hpp"
#include "title_info.hpp"
#include <map>
#include <cstdio>
namespace nexus::transfer {
namespace {
struct Index {std::map<std::string,u64> titles;u64 generation{};};
std::map<Target,Index> indices;
constexpr std::string_view ExportRoot="/installed-games";
Result Find(Target source,const std::string& path,std::string& folder,bool& file){
    R_UNLESS(indices.contains(source) && lifecycle::CanPoll() && path.starts_with(std::string(ExportRoot)+"/"),Invalid);
    auto relative=path.substr(ExportRoot.size()+1);auto slash=relative.find('/');folder=relative.substr(0,slash);file=slash!=relative.npos;
    R_UNLESS(indices.at(source).titles.contains(folder) && (!file || relative.substr(slash+1)=="Game.nsp"),Invalid);return 0;
}
}
void ResetExports(){indices.clear();}
bool ExportsReady(Target target){return indices.contains(target);}
u64 ExportGeneration(Target target){return indices.contains(target)?indices.at(target).generation:0;}
Result PrepareExports(sphaira::ui::ProgressBox* p,Target target){
    R_UNLESS(Active()==Mode::Off && nexus::ValidTarget(target),Invalid);indices.erase(target);u64 generation{};
    NcmContentStorage cs{};R_TRY(sphaira::nexusapp::OpenStorage(target,cs,&generation));ncmContentStorageClose(&cs);
    std::vector<sphaira::nexusapp::Game> games;R_TRY(sphaira::nexusapp::ScanGames(p,games));
    std::map<std::string,u64> found;size_t checked{};
    for(const auto& game:games){
        R_TRY(p->ShouldExitResult());std::vector<sphaira::nexusapp::DumpRecord> records;
        const auto rc=sphaira::nexusapp::CollectDumpRecords(game.id,target,generation,records);
        // Inventory doesn't read/decrypt all content. Exact package/license
        // validation occurs when the PC opens this game's folder/file.
        if(R_SUCCEEDED(rc) && !records.empty()){
            auto name=sphaira::title::GetCachedTitleName(game.id);if(name.empty())name="Game";
            char id[24];std::snprintf(id,sizeof(id)," [%016llX]",static_cast<unsigned long long>(game.id));
            found.emplace(nexus::dump::SafeName(name)+id,game.id);
        }
        p->SetOverallTransfer(++checked,std::max<size_t>(games.size(),1),"Listing installed game exports");
    }
    if(target==Target::Usb){Client c;R_TRY(c.Check(generation));}
    R_TRY(p->ShouldExitResult());indices.emplace(target,Index{std::move(found),generation});return 0;
}
Result OpenExport(Target source,const std::string& path,std::unique_ptr<sphaira::nexusapp::VirtualNsp>& out){
    std::string folder;bool file{};R_TRY(Find(source,path,folder,file));R_UNLESS(file,Invalid);const auto& index=indices.at(source);
    auto nsp=std::make_unique<sphaira::nexusapp::VirtualNsp>();R_TRY(nsp->Open(index.titles.at(folder),source,index.generation));out=std::move(nsp);return 0;
}
Result ExportType(Target source,const std::string& path,Info& info){
    R_UNLESS(indices.contains(source) && lifecycle::CanPoll(),Invalid);info={};if(path==ExportRoot){info.directory=true;return 0;}
    std::string folder;bool file{};R_TRY(Find(source,path,folder,file));info.directory=!file;
    if(file){std::unique_ptr<sphaira::nexusapp::VirtualNsp> nsp;R_TRY(OpenExport(source,path,nsp));info.size=nsp->Size();}return 0;
}
Result ExportList(Target source,const std::string& path,std::vector<Entry>& entries){
    R_UNLESS(indices.contains(source) && lifecycle::CanPoll(),Invalid);entries.clear();
    if(path==ExportRoot){for(const auto& [folder,id]:indices.at(source).titles){Entry e{};std::snprintf(e.name,sizeof(e.name),"%s",folder.c_str());e.directory=true;entries.push_back(e);}return 0;}
    std::string folder;bool file{};R_TRY(Find(source,path,folder,file));R_UNLESS(!file,Invalid);
    Info info{};R_TRY(ExportType(source,path+"/Game.nsp",info));Entry e{};std::snprintf(e.name,sizeof(e.name),"Game.nsp");e.size=info.size;entries.push_back(e);return 0;
}
}
