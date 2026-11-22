// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include "nexus/file_manager_core.hpp"
#include <optional>
namespace nexus::ports {
namespace files=nexus::files;
inline constexpr std::string_view Store="/nexus-port-manager";
inline constexpr std::size_t MaxModFiles=8192;
inline constexpr std::uint64_t MaxModBytes=64ULL*1024*1024*1024;
inline bool Hex(std::string_view s,std::size_t size){return s.size()==size&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool Root(std::string_view path){
    if(!files::Safe(path))return false;
    const auto p=files::Fold(std::string(path));
    for(auto root:{"/bootloader","/config","/sept","/nexus-port-manager"})if(files::Within(p,root))return false;
    if(p=="/switch"||p=="/homebrew"||p=="/games")return false;
    if(files::Within(p,"/atmosphere")){
        constexpr std::string_view prefix="/atmosphere/contents/";
        if(!p.starts_with(prefix)||p.size()<prefix.size()+16)return false;
        const auto id=std::string_view(p).substr(prefix.size(),16);
        // Application content only. Never expose system-module trees as ports.
        if(!Hex(id,16)||!(id.starts_with("0100")||id.starts_with("05"))||id.starts_with("010000000000")||id=="05004e584d470000")return false;
        return p.size()==prefix.size()+16||p[prefix.size()+16]=='/';
    }
    return true;
}
inline bool Relative(std::string_view path){return !path.empty()&&path.front()!='/'&&files::Canonical("/"+std::string(path))&&path.size()<192;}
inline bool Overlap(std::string a,std::string b){a=files::Fold(a);b=files::Fold(b);return files::Within(a,b)||files::Within(b,a);}
inline std::string DefaultRoot(std::string_view nro){
    if(!Root(nro))return {};
    if(nro.starts_with("/switch/")){const auto slash=nro.find('/',8);return std::string(nro.substr(0,slash==nro.npos?nro.size():slash));}
    const auto parent=files::Parent(nro);
    return Root(parent)?parent:std::string(nro);
}
inline std::string ArchivePath(std::string path,const std::string& root){
    if(!Relative(path))return {};
    const auto prefix=root.substr(1)+"/";
    if(path.starts_with(prefix))path.erase(0,prefix.size());
    else if(path.starts_with("atmosphere/")||path.starts_with("switch/"))return {};
    return Relative(path)?path:std::string{};
}
struct ModFile {std::string relative,hash;std::uint64_t size{};bool had_original{};std::string original_hash;std::uint64_t original_size{};};
struct ModPlan {std::string root,store,name;std::vector<ModFile> files;std::uint64_t bytes{};};
inline bool Valid(const ModPlan& plan){
    if(!Root(plan.root)||!files::Canonical(plan.store)||!plan.store.starts_with(std::string(Store)+"/")||plan.files.empty()||plan.files.size()>MaxModFiles)return false;
    std::set<std::string> names;std::uint64_t bytes{};
    for(const auto& f:plan.files){
        if(!Relative(f.relative)||!Hex(f.hash,64)||(f.had_original&&!Hex(f.original_hash,64))||f.size>MaxModBytes-bytes||f.original_size>MaxModBytes)return false;
        const auto target=files::Child(plan.root,f.relative);
        if(!Root(target)||!files::Canonical(plan.store+"/payload/"+f.relative)||!files::Canonical(plan.store+"/original/"+f.relative))return false;
        if(!names.insert(files::Fold(f.relative)).second)return false;bytes+=f.size;
    }
    for(const auto& name:names)for(auto at=name.find('/');at!=name.npos;at=name.find('/',at+1))if(names.contains(name.substr(0,at)))return false;
    return bytes==plan.bytes;
}
// Each rename remains on one physical volume. The immutable session records
// original hashes before the first change; interrupted enables use Disable.
// Match distinguishes missing from I/O failure and refuses edited live files.
template<class IO> void Progress(IO& io,std::size_t done,std::size_t total){
    if constexpr(requires {io.Progress(done,total);})io.Progress(done,total);
}
template<class IO> bool Enable(IO& io,const ModPlan& p){
    if(!Valid(p))return io.Fail("Invalid mod plan. Nothing was changed."_nx);
    std::size_t done{};const auto total=p.files.size()*2+1;Progress(io,done,total);
    for(const auto& f:p.files){
        if(!io.Check()||!io.Match(p.store+"/payload/"+f.relative,true,f.size,f.hash)||
           !io.Match(files::Child(p.root,f.relative),f.had_original,f.original_size,f.original_hash)||
           !io.Match(p.store+"/original/"+f.relative,false,0,{}))return false;
        Progress(io,++done,total);
    }
    if(!io.Begin(p))return false;
    for(const auto& f:p.files){
        const auto dst=files::Child(p.root,f.relative),backup=p.store+"/original/"+f.relative,src=p.store+"/payload/"+f.relative;
        if(!io.Check()||!io.Match(dst,f.had_original,f.original_size,f.original_hash)||!io.Match(src,true,f.size,f.hash))return false;
        if(!io.Parents(dst)||!io.Parents(backup))return false;
        if(f.had_original&&!io.Rename(dst,backup))return false;
        if(!io.Rename(src,dst)||!io.Match(dst,true,f.size,f.hash)||!io.Flush())return false;
        Progress(io,++done,total);
    }
    const auto ok=io.Activated();if(ok)Progress(io,total,total);return ok;
}
template<class IO> bool RestoreFile(IO& io,const ModPlan& p,const ModFile& f,bool apply){
    const auto dst=files::Child(p.root,f.relative),src=p.store+"/payload/"+f.relative,backup=p.store+"/original/"+f.relative;
    bool live{},payload{},old{};
    if(!io.Check()||!io.Exists(dst,live)||!io.Exists(src,payload)||!io.Exists(backup,old))return false;
    if(old&&!f.had_original)return io.Fail("Unexpected original-file backup. Files were kept."_nx);
    if(old&&!io.Match(backup,true,f.original_size,f.original_hash))return false;
    if(payload){
        if(!io.Match(src,true,f.size,f.hash))return false;
        if(live){
            if(old||!f.had_original||!io.Match(dst,true,f.original_size,f.original_hash))return io.Fail("A port file changed after the mod was enabled. Keep it and resolve the conflict before restoring."_nx);
            return true; // not changed yet, or already restored
        }
        if(f.had_original&&!old)return io.Fail("An original file is missing. Recovery stopped without guessing."_nx);
    }else{
        if(!live||!io.Match(dst,true,f.size,f.hash)||f.had_original!=old)return io.Fail("Mod files changed or are missing. Recovery has kept the remaining files."_nx);
        if(apply&&(!io.Parents(src)||!io.Rename(dst,src)))return false;
    }
    if(apply&&old&&!io.Rename(backup,dst))return false;
    return !apply||io.Flush();
}
template<class IO> bool Disable(IO& io,const ModPlan& p){
    if(!Valid(p))return io.Fail("Invalid recovery record. Files were kept."_nx);
    std::size_t done{};const auto total=p.files.size()*2+1;Progress(io,done,total);
    // Reject conflicts anywhere before starting restoration.
    for(const auto& f:p.files){if(!RestoreFile(io,p,f,false))return false;Progress(io,++done,total);}
    for(auto i=p.files.rbegin();i!=p.files.rend();++i){if(!RestoreFile(io,p,*i,true))return false;Progress(io,++done,total);}
    const auto ok=io.Finished();if(ok)Progress(io,total,total);return ok;
}
}
