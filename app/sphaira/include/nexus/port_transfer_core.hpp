// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/vsd_policy.hpp"
#include "nexus/port_progress.hpp"
#include <span>
#include "nexus/manager_backup.hpp"
#include <array>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace nexus::port {
using Digest=std::array<std::uint8_t,32>;
inline std::string Fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline bool Within(std::string_view p,std::string_view root){return p==root || (p.starts_with(root)&&p.size()>root.size()&&p[root.size()]=='/');}
inline bool Safe(std::string_view p){
    if(p.size()>=256 || !nexus_vsd::Canonical(p) || p=="/" || !manager::SafeRelative(p.substr(1)))return false;
    const auto lower=Fold(std::string(p));
    for(auto root:{"/nintendo","/atmosphere","/bootloader","/emummc","/registered","/placehld","/private","/config",
                   "/switch/nexusmanager","/switch/usbloader","/switch/sphaira"})if(Within(lower,root))return false;
    return lower!="/switch" && lower!="/homebrew";
}
inline std::string DefaultRoot(std::string_view nro){
    if(!Safe(nro)||!nexus_vsd::Nro(nro))return {};
    // Include the entire app folder, including nested runtimes and game data.
    if(nro.starts_with("/switch/")){
        auto end=nro.find('/',8);return std::string(nro.substr(0,end==nro.npos?nro.size():end));
    }
    const auto end=nro.find_last_of('/');return end?std::string(nro.substr(0,end)):std::string(nro);
}
struct Entry {std::string path;std::uint64_t size{};bool directory{};auto operator<=>(const Entry&)const=default;};
struct Plan {
    bool source_usb{},move{},create_launcher{true};
    std::uint64_t generation{},application{},bytes{};
    unsigned address_space{3};
    std::string name,nro,args,source_root,destination_root;
    std::vector<std::string> roots;
    std::vector<std::string> retained_roots;
    std::vector<Entry> entries;
    std::string Source(std::string_view p)const{return source_root+std::string(p);}
    std::string Destination(std::string_view p)const{return destination_root+std::string(p);}
};
struct Report {
    Phase phase{Phase::Preparing};std::string path,detail;std::size_t copied{},reused{},removed{},retained{};bool launcher{};
    std::string Message(bool move)const{
        if(phase==Phase::Complete)return std::string(move?"Port move complete.":"Port copy complete. Both file copies were kept.")+
            (launcher?"\nA new HOME shortcut was created for the destination. The old shortcut was kept; remove it in System Settings after testing the new one.":"\nFiles are ready on the destination. HOME shortcuts were not changed. You can create a destination shortcut separately.")+
            (retained?"\nShared runtime files were kept on the source for other ports.":"")+"\n\nPort saves/settings inside the selected folders were included. Console save data was not changed."+(detail.empty()?"":"\n"+detail);
        return "Port transfer stopped"+(path.empty()?std::string{}:" at "+path)+".\n"+detail+
            (removed?"\nSome source files were removed. The verified destination was kept.":"\nThe original files were kept.")+
            (launcher?"\nThe new destination shortcut was installed.":"\nDo not use an incomplete destination as a finished port.");
    }
};
// All mutations are behind this production engine. The host harness injects
// failures into the same readback, publication and source-removal sequence.
template<class IO> auto Inventory(IO& io,Plan& plan){
    using R=typename IO::Result;
    plan.entries.clear();plan.bytes=0;std::set<std::string> seen;
    for(const auto& root:plan.roots){
        if(!Safe(root))return R{IO::Invalid};
        std::vector<std::pair<std::string,unsigned>> pending{{root,0}};
        for(std::size_t at=0;at<pending.size();++at){
            if(auto rc=io.Check())return rc;
            const auto [path,depth]=pending[at];
            if(depth>64 || !Safe(path) || !Safe(plan.Source(path)) || !Safe(plan.Destination(path)))return R{IO::Invalid};
            if(!seen.insert(Fold(path)).second)return R{IO::Invalid};
            Entry e;bool exists{};
            if(auto rc=io.Stat(plan.source_usb,plan.Source(path),e,exists))return rc;
            if(!exists || plan.entries.size()>=131072 || e.size>(1ULL<<40)-plan.bytes)return R{IO::Invalid};
            e.path=path;plan.bytes+=e.directory?0:e.size;plan.entries.push_back(e);
            if(e.directory){
                std::vector<std::string> names;
                if(auto rc=io.List(plan.source_usb,plan.Source(path),names))return rc;
                for(const auto& name:names){if(name.empty()||name.find('/')!=name.npos)return R{IO::Invalid};pending.emplace_back(path+"/"+name,depth+1);}
            }
        }
    }
    std::sort(plan.entries.begin(),plan.entries.end());
    if(std::none_of(plan.entries.begin(),plan.entries.end(),[&](const Entry& e){return e.path==plan.nro&&!e.directory;}))return R{IO::Invalid};
    return R{};
}
template<class IO> auto HashFile(IO& io,bool usb,const std::string& path,std::uint64_t size,Digest& digest,std::span<std::uint8_t> buffer,Progress& progress){
    using R=typename IO::Result;typename IO::Hash hash;
    progress.path=path;io.Progress(progress);
    if(buffer.empty())return R{IO::Invalid};
    Entry entry;bool exists{};
    if(auto rc=io.Stat(usb,path,entry,exists))return rc;
    if(!exists||entry.directory||entry.size!=size)return R{IO::Invalid};
    for(std::uint64_t off=0;off<size;){
        if(auto rc=io.Check())return rc;
        const auto count=std::min<std::uint64_t>(buffer.size(),size-off);std::uint64_t actual{};
        if(auto rc=io.Read(usb,path,off,buffer.data(),count,actual))return rc;
        if(actual!=count)return R{IO::Invalid};
        hash.Update(buffer.data(),count);off+=count;progress.done+=count;io.Progress(progress);
    }
    io.EndRead();digest=hash.Final();return io.Check();
}
template<class IO> auto Execute(IO& io,const Plan& plan,Report& report){
    using R=typename IO::Result;report={};
    Progress progress;progress.total=plan.bytes;progress.move=plan.move;io.Progress(progress);
    std::vector<std::uint8_t> buffer(io.BufferSize());
    if(buffer.empty())return R{IO::Invalid};
    auto current=plan;
    if(auto rc=Inventory(io,current))return rc;
    if(current.entries!=plan.entries || current.bytes!=plan.bytes)return R{IO::Invalid};
    std::vector<Digest> hashes(plan.entries.size());std::vector<bool> reuse(plan.entries.size());std::uint64_t required{};
    report.detail="Checking source files, destination conflicts and available space";
    // Preflight the WHOLE selection before writing any destination file.
    for(std::size_t i=0;i<plan.entries.size();++i){
        const auto& e=plan.entries[i];report.path=e.path;
        progress.path=e.path;io.Progress(progress);
        Entry dst;bool exists{};if(auto rc=io.Stat(!plan.source_usb,plan.Destination(e.path),dst,exists))return rc;
        if(exists && (e.directory!=dst.directory || (!e.directory&&e.size!=dst.size)))return R{IO::Conflict};
        if(e.directory)continue;
        // Existing files must match before any destination writes. For new files,
        // hash the source during copying instead of reading the whole game twice.
        if(exists){Digest hash;
            if(auto rc=HashFile(io,plan.source_usb,plan.Source(e.path),e.size,hashes[i],buffer,progress))return rc;
            if(auto rc=HashFile(io,!plan.source_usb,plan.Destination(e.path),e.size,hash,buffer,progress))return rc;
            if(hash!=hashes[i])return R{IO::Conflict};
            reuse[i]=true;progress.reused+=e.size;io.Progress(progress);
        }else required+=e.size;
    }
    if(auto rc=io.Preflight(required))return rc;
    report.phase=Phase::Copying;report.detail="Copying port files";
    progress.Start(Phase::Copying,plan.bytes);progress.done=progress.reused;io.Progress(progress);
    for(std::size_t i=0;i<plan.entries.size();++i){
        const auto& e=plan.entries[i];report.path=e.path;progress.path=e.path;io.Progress(progress);if(auto rc=io.Check())return rc;
        if(e.directory){if(auto rc=io.Directory(!plan.source_usb,plan.Destination(e.path)))return rc;continue;}
        if(reuse[i]){++report.reused;continue;}
        std::string temp;if(auto rc=io.CreateTemporary(i,e.size,temp))return rc;
        typename IO::Hash hash;
        for(std::uint64_t off=0;off<e.size;){
            if(auto rc=io.Check())return rc;
            const auto n=std::min<std::uint64_t>(buffer.size(),e.size-off);std::uint64_t got{};
            if(auto rc=io.Read(plan.source_usb,plan.Source(e.path),off,buffer.data(),n,got))return rc;
            if(got!=n)return R{IO::Invalid};
            hash.Update(buffer.data(),n);
            if(auto rc=io.WriteTemporary(temp,off,buffer.data(),n))return rc;
            off+=n;progress.copied+=n;progress.done+=n;io.Progress(progress);
        }
        io.EndRead();hashes[i]=hash.Final();
        // Durability before publication; one complete readback below verifies the
        // published name too. Never install a launcher or delete originals first.
        if(auto rc=io.Flush())return rc;
        if(auto rc=io.Publish(temp,plan.Destination(e.path)))return rc;
        ++report.copied;
    }
    if(auto rc=io.Flush())return rc;
    // A full-folder copy must also reject files added, removed or resized while
    // copying. This metadata walk does not reread the game's payload.
    if(auto rc=Inventory(io,current))return rc;
    if(current.entries!=plan.entries || current.bytes!=plan.bytes)return R{IO::Invalid};
    report.phase=Phase::Verifying;report.detail="Verifying all destination files";
    progress.Start(Phase::Verifying,plan.bytes);io.Progress(progress);
    // Verify all published/reused files before installing a launcher.
    for(std::size_t i=0;i<plan.entries.size();++i)if(!plan.entries[i].directory){
        const auto& e=plan.entries[i];report.path=e.path;Digest hash;
        if(auto rc=HashFile(io,!plan.source_usb,plan.Destination(e.path),e.size,hash,buffer,progress))return rc;
        if(hash!=hashes[i])return R{IO::Invalid};
    }
    if(plan.create_launcher){
        report.phase=Phase::Launcher;report.path.clear();report.detail="Creating the destination HOME shortcut";
        progress.Start(Phase::Launcher);io.Progress(progress);
        if(auto rc=io.InstallLauncher())return rc;
        report.launcher=true;
    }
    if(plan.move){
        report.phase=Phase::CheckingMove;report.detail="Rechecking both copies before removing source files";
        progress.Start(Phase::CheckingMove,plan.bytes*2);io.Progress(progress);
        if(auto rc=Inventory(io,current))return rc;
        if(current.entries!=plan.entries)return R{IO::Invalid};
        for(std::size_t i=0;i<plan.entries.size();++i)if(!plan.entries[i].directory){
            const auto& e=plan.entries[i];report.path=e.path;Digest a,b;
            if(auto rc=HashFile(io,plan.source_usb,plan.Source(e.path),e.size,a,buffer,progress))return rc;
            if(auto rc=HashFile(io,!plan.source_usb,plan.Destination(e.path),e.size,b,buffer,progress))return rc;
            if(a!=hashes[i]||b!=a)return R{IO::Invalid};
        }
        report.phase=Phase::Removing;report.detail="Removing verified source files; the destination is complete";
        std::uint64_t removal_bytes{};
        for(const auto& e:plan.entries)if(!e.directory && !std::any_of(plan.retained_roots.begin(),plan.retained_roots.end(),
            [&](const std::string& root){return Within(Fold(e.path),Fold(root));}))removal_bytes+=e.size;
        progress.Start(Phase::Removing,removal_bytes*2);io.Progress(progress);
        for(std::size_t i=0;i<plan.entries.size();++i)if(!plan.entries[i].directory){
            const auto& e=plan.entries[i];report.path=e.path;progress.path=e.path;io.Progress(progress);if(auto rc=io.Check())return rc;
            if(std::any_of(plan.retained_roots.begin(),plan.retained_roots.end(),[&](const std::string& root){return Within(Fold(e.path),Fold(root));})){++report.retained;continue;}
            // Rehash the exact pair immediately before each non-recursive delete.
            Digest a,b;if(auto rc=HashFile(io,plan.source_usb,plan.Source(e.path),e.size,a,buffer,progress))return rc;
            if(auto rc=HashFile(io,!plan.source_usb,plan.Destination(e.path),e.size,b,buffer,progress))return rc;
            if(a!=hashes[i]||b!=a)return R{IO::Invalid};
            if(auto rc=io.RemoveSource(plan.Source(e.path),e.size))return rc;
            ++report.removed;
        }
        // Empty source directories are harmless; never recurse during deletion.
    }
    report.phase=Phase::Complete;report.path.clear();report.detail.clear();progress.Start(Phase::Complete);io.Progress(progress);return R{};
}
}
