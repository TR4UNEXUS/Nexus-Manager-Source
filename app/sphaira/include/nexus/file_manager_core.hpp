// SPDX-License-Identifier: GPL-3.0-or-later
// Portable production engine. Platform IO never chooses a fallback volume.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace nexus::files {
using Digest=std::array<std::uint8_t,32>;
inline constexpr std::size_t MaxEntries=16384,MaxDepth=32,MaxPath=256;
inline constexpr std::uint64_t MaxBytes=1ULL<<40;
inline constexpr std::string_view StagePrefix=".nexus-fm-";
inline std::string Fold(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
inline bool Within(std::string_view path,std::string_view root){return path==root || (path.size()>root.size()&&path.starts_with(root)&&path[root.size()]=='/');}
inline bool Utf8(std::string_view s){
    for(std::size_t i=0;i<s.size();){const auto c=static_cast<unsigned char>(s[i++]);if(c<128)continue;
        unsigned count{};std::uint32_t value{},minimum{};
        if(c>=0xc2&&c<=0xdf){count=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;value=c&7;minimum=0x10000;}else return false;
        if(count>s.size()-i)return false;while(count--){const auto next=static_cast<unsigned char>(s[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }return true;
}
inline bool Canonical(std::string_view path){
    if(path.empty()||path.size()>=MaxPath||path.front()!='/'||!Utf8(path))return false;
    if(path=="/")return true;
    if(path.back()=='/')return false;
    for(std::size_t pos=1;pos<path.size();){
        auto end=path.find('/',pos);if(end==path.npos)end=path.size();const auto part=path.substr(pos,end-pos);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
        for(unsigned char c:part)if(c<32||c==127||std::string_view("\\:*?\"<>|").find(c)!=std::string_view::npos)return false;
        pos=end+1;
    }return true;
}
inline std::string Parent(std::string_view path){const auto p=path.find_last_of('/');return p?std::string(path.substr(0,p)):"/";}
inline std::string Leaf(std::string_view path){return std::string(path.substr(path.find_last_of('/')+1));}
inline std::string Child(std::string_view parent,std::string_view name){return (parent=="/"?"":std::string(parent))+"/"+std::string(name);}
inline bool Protected(std::string_view path){
    const auto lower=Fold(std::string(path));
    for(auto root:{"/nintendo","/emummc","/registered","/placehld","/private","/config/nexusmanager",
                  "/switch/nexusmanager","/switch/usbloader","/switch/sphaira"})
        if(Within(lower,root))return true;
    for(std::size_t at=1;at<lower.size();){auto end=lower.find('/',at);if(end==lower.npos)end=lower.size();
        const auto part=std::string_view(lower).substr(at,end-at);
        if(part.starts_with(StagePrefix)||part=="registered"||part=="placehld")return true;at=end+1;}
    for(auto file:{"/switch/nexusmanager.nro","/nexusmanager.nro","/switch/usbloader.nro","/switch/sphaira.nro"})if(lower==file)return true;
    return false;
}
inline bool Safe(std::string_view path){return path!="/"&&Canonical(path)&&!Protected(path);}
inline bool SafeDirectory(std::string_view path){return Canonical(path)&&!Protected(path);}
inline bool StagePath(std::string_view path){
    if(!Canonical(path)||!SafeDirectory(Parent(path)))return false;
    const auto leaf=Leaf(path);if(!leaf.starts_with(StagePrefix)||leaf.size()!=StagePrefix.size()+32)return false;
    return std::all_of(leaf.begin()+StagePrefix.size(),leaf.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
struct Entry {std::string path;std::uint64_t size{};bool directory{};Digest hash{};};
struct Snapshot {std::string root;std::vector<Entry> entries;std::uint64_t bytes{};bool staging{};};
struct Report {std::string detail,staged_path;std::size_t copied{},removed{};bool published{},complete{},removal_started{};};
inline bool SameInventory(const Snapshot& a,const Snapshot& b){
    if(a.root!=b.root||a.staging!=b.staging||a.bytes!=b.bytes||a.entries.size()!=b.entries.size())return false;
    for(std::size_t i=0;i<a.entries.size();++i){const auto& x=a.entries[i];const auto& y=b.entries[i];
        if(x.path!=y.path||x.size!=y.size||x.directory!=y.directory)return false;}return true;
}
inline bool SnapshotPath(const Snapshot& s,std::string_view path){
    if(!s.staging)return Safe(path);
    return StagePath(s.root)&&Within(path,s.root)&&Canonical(path)&&Safe("/reviewed-stage"+std::string(path.substr(s.root.size())));
}
inline bool ValidSnapshot(const Snapshot& s){
    if(!SnapshotPath(s,s.root)||s.entries.empty()||s.entries.size()>MaxEntries||!s.entries[0].path.empty()||s.bytes>MaxBytes)return false;
    std::uint64_t bytes{};std::set<std::string> seen,dirs;
    for(std::size_t i=0;i<s.entries.size();++i){const auto& e=s.entries[i];const auto full=s.root+e.path;
        if(!SnapshotPath(s,full)||(i&&(!e.path.starts_with('/')||e.path=="/"))||!seen.insert(Fold(full)).second||e.size>MaxBytes-bytes)return false;
        if(e.directory&&e.size)return false;bytes+=e.size;
        if(i&&(!s.entries[0].directory||s.entries[i-1].path>=e.path))return false;
        if(i){const auto parent=Parent(e.path);const auto wanted=parent=="/"?"":parent;if(!dirs.contains(wanted))return false;}
        if(e.directory)dirs.insert(e.path);
    }return bytes==s.bytes;
}
inline bool ValidDestination(bool source_usb,bool destination_usb,const Snapshot& s,std::string_view final){
    if(!ValidSnapshot(s)||s.staging||!Safe(s.root)||!Safe(final)||!SafeDirectory(Parent(final)))return false;
    if(source_usb==destination_usb){const auto a=Fold(s.root),b=Fold(std::string(final));if(Within(a,b)||Within(b,a))return false;}
    const auto stage=Child(Parent(final),std::string(StagePrefix)+std::string(32,'0'));
    for(const auto& e:s.entries)if(!Safe(std::string(final)+e.path)||!Canonical(stage+e.path))return false;
    return true;
}
template<class IO> auto HashFile(IO& io,bool usb,const std::string& path,std::uint64_t size,Digest& out,std::span<std::uint8_t> buffer){
    using R=typename IO::Result;if(buffer.empty())return R{IO::Invalid};typename IO::Hash hash;
    io.Progress("Checking",path,0,size);
    Entry e;bool exists{};if(auto rc=io.Stat(usb,path,e,exists))return rc;
    if(!exists||e.directory||e.size!=size)return R{IO::Invalid};
    for(std::uint64_t off=0;off<size;){if(auto rc=io.Check())return rc;
        const auto n=std::min<std::uint64_t>(buffer.size(),size-off);std::uint64_t got{};
        if(auto rc=io.Read(usb,path,off,buffer.data(),n,got))return rc;
        if(got!=n)return R{IO::Invalid};hash.Update(buffer.data(),n);off+=n;io.Progress("Checking",path,off,size);}
    io.EndRead();if(auto rc=io.Stat(usb,path,e,exists))return rc;
    if(!exists||e.directory||e.size!=size)return R{IO::Invalid};out=hash.Final();return R{};
}
// Existing path components must use names returned by directory enumeration.
// This supports UTF-8 without guessing filesystem Unicode case-folding rules,
// and refuses short-name/case aliases before same-volume ancestry decisions.
template<class IO> auto CheckSpelling(IO& io,bool usb,const std::string& path){
    using R=typename IO::Result;if(!Canonical(path))return R{IO::Invalid};std::string parent="/";
    for(std::size_t at=1;at<path.size();){auto end=path.find('/',at);if(end==path.npos)end=path.size();const auto name=path.substr(at,end-at);
        std::vector<std::string> names;if(auto rc=io.List(usb,parent,names))return rc;
        if(std::count(names.begin(),names.end(),name)!=1)return R{IO::Invalid};parent=Child(parent,name);at=end+1;
    }return R{};
}
template<class IO> auto Inventory(IO& io,bool usb,const std::string& root,Snapshot& out,bool staging=false){
    using R=typename IO::Result;out={};out.root=root;out.staging=staging;if(!SnapshotPath(out,root))return R{IO::Invalid};
    if(auto rc=CheckSpelling(io,usb,root))return rc;
    std::vector<std::pair<std::string,unsigned>> pending{{"",0}};std::set<std::string> seen;
    for(std::size_t at=0;at<pending.size();++at){if(auto rc=io.Check())return rc;const auto [relative,depth]=pending[at];const auto full=root+relative;
        io.Progress("Reading folders",full,0,0);
        if(depth>MaxDepth||!SnapshotPath(out,full)||!seen.insert(Fold(full)).second)return R{IO::Invalid};
        Entry e;bool exists{};if(auto rc=io.Stat(usb,full,e,exists))return rc;
        if(!exists||e.size>MaxBytes-out.bytes)return R{IO::Invalid};e.path=relative;if(e.directory)e.size=0;out.bytes+=e.size;out.entries.push_back(e);
        if(e.directory){std::vector<std::string> names;if(auto rc=io.List(usb,full,names))return rc;
            if(names.size()>MaxEntries-pending.size())return R{IO::Invalid};
            for(const auto& name:names){if(name.empty()||name.find('/')!=name.npos)return R{IO::Invalid};pending.emplace_back(relative+"/"+name,depth+1);}}
    }
    std::sort(out.entries.begin(),out.entries.end(),[](const Entry& a,const Entry& b){return a.path<b.path;});
    return R{ValidSnapshot(out)?0:IO::Invalid};
}
template<class IO> auto Prepare(IO& io,bool usb,const std::string& root,Snapshot& out,bool staging=false){
    using R=typename IO::Result;if(auto rc=Inventory(io,usb,root,out,staging))return rc;
    std::vector<std::uint8_t> buffer(io.BufferSize());
    // A retained temporary item is explicitly being discarded, not treated as
    // valid user data. Review its exact names/types/sizes without reading a
    // potentially huge preallocated, incomplete payload.
    if(!staging)for(auto& e:out.entries)if(!e.directory)if(auto rc=HashFile(io,usb,root+e.path,e.size,e.hash,buffer))return rc;
    Snapshot again;if(auto rc=Inventory(io,usb,root,again,staging))return rc;return R{SameInventory(out,again)?0:IO::Invalid};
}
template<class IO> auto Recheck(IO& io,bool usb,const Snapshot& s,std::span<std::uint8_t> buffer){
    using R=typename IO::Result;Snapshot current;if(auto rc=Inventory(io,usb,s.root,current,s.staging))return rc;
    if(!SameInventory(s,current))return R{IO::Invalid};
    for(const auto& e:s.entries)if(!e.directory){Digest digest;if(auto rc=HashFile(io,usb,s.root+e.path,e.size,digest,buffer))return rc;if(digest!=e.hash)return R{IO::Invalid};}
    if(auto rc=Inventory(io,usb,s.root,current,s.staging))return rc;return R{SameInventory(s,current)?0:IO::Invalid};
}
template<class IO> auto RemoveReviewed(IO& io,bool usb,const Snapshot& s,Report& report,std::span<std::uint8_t> buffer,
                                     bool paired=false,bool other_usb=false,const std::string& other_root={}){
    using R=typename IO::Result;
    for(const auto& e:s.entries)if(!e.directory){if(auto rc=io.Check())return rc;Digest digest;
        if(s.staging){Entry current;bool exists{};if(auto rc=io.Stat(usb,s.root+e.path,current,exists))return rc;
            if(!exists||current.directory||current.size!=e.size)return R{IO::Invalid};}
        else {if(auto rc=HashFile(io,usb,s.root+e.path,e.size,digest,buffer))return rc;if(digest!=e.hash)return R{IO::Invalid};}
        if(paired){if(auto rc=HashFile(io,other_usb,other_root+e.path,e.size,digest,buffer))return rc;if(digest!=e.hash)return R{IO::Invalid};}
        io.Progress("Removing",s.root+e.path,report.removed,s.entries.size());
        report.removal_started=true;if(auto rc=io.Remove(usb,s.root+e.path,e.size,false))return rc;++report.removed;
    }
    for(auto it=s.entries.rbegin();it!=s.entries.rend();++it)if(it->directory){
        if(auto rc=io.Check())return rc;std::vector<std::string> names;if(auto rc=io.List(usb,s.root+it->path,names))return rc;
        if(!names.empty())return R{IO::Invalid};report.removal_started=true;if(auto rc=io.Remove(usb,s.root+it->path,0,true))return rc;++report.removed;
    }return io.Flush(usb);
}
template<class IO> auto Transfer(IO& io,bool source_usb,bool destination_usb,const Snapshot& s,const std::string& final,bool move,Report& report){
    using R=typename IO::Result;report={};report.detail="Checking the reviewed source and destination";
    if(!ValidDestination(source_usb,destination_usb,s,final))return R{IO::Invalid};
    if(auto rc=CheckSpelling(io,destination_usb,Parent(final)))return rc;
    if(auto rc=io.Check())return rc;Entry exists_entry;bool exists{};
    if(auto rc=io.Stat(destination_usb,final,exists_entry,exists))return rc;if(exists){report.detail="The destination name already exists. Nothing was replaced.";return R{IO::Conflict};}
    Snapshot current;if(auto rc=Inventory(io,source_usb,s.root,current))return rc;if(!SameInventory(s,current))return R{IO::Invalid};
    if(auto rc=io.Preflight(s.bytes))return rc;
    std::vector<std::uint8_t> buffer(io.BufferSize());if(buffer.empty())return R{IO::Invalid};
    report.detail="Copying into a new temporary item";
    if(auto rc=io.NewStage(s.entries[0].directory,s.entries[0].size,report.staged_path))return rc;
    if(!StagePath(report.staged_path)||Parent(report.staged_path)!=Parent(final))return R{IO::Invalid};
    for(const auto& e:s.entries){const auto path=report.staged_path+e.path;if(!Canonical(path))return R{IO::Invalid};
        if(auto rc=io.Check())return rc;
        if(e.directory){if(!e.path.empty())if(auto rc=io.Directory(destination_usb,path))return rc;continue;}
        if(!e.path.empty())if(auto rc=io.Create(destination_usb,path,e.size))return rc;typename IO::Hash hash;
        for(std::uint64_t off=0;off<e.size;){if(auto rc=io.Check())return rc;const auto n=std::min<std::uint64_t>(buffer.size(),e.size-off);std::uint64_t got{};
            if(auto rc=io.Read(source_usb,s.root+e.path,off,buffer.data(),n,got))return rc;if(got!=n)return R{IO::Invalid};hash.Update(buffer.data(),n);
            if(auto rc=io.Write(destination_usb,path,off,buffer.data(),n))return rc;off+=n;io.Progress("Copying",s.root+e.path,off,e.size);}
        io.EndRead();if(hash.Final()!=e.hash)return R{IO::Invalid};if(auto rc=io.Flush(destination_usb))return rc;++report.copied;
    }
    report.detail="Verifying the temporary copy";
    for(const auto& e:s.entries)if(!e.directory){Digest digest;if(auto rc=HashFile(io,destination_usb,report.staged_path+e.path,e.size,digest,buffer))return rc;if(digest!=e.hash)return R{IO::Invalid};}
    Snapshot staged=s;staged.root=report.staged_path;staged.staging=true;
    if(auto rc=Inventory(io,destination_usb,staged.root,current,true))return rc;if(!SameInventory(staged,current))return R{IO::Invalid};
    // Recheck the source tree before publishing. A rename is exclusive and
    // publishes the whole folder in one operation; no merging or overwrite.
    if(auto rc=Inventory(io,source_usb,s.root,current))return rc;if(!SameInventory(s,current))return R{IO::Invalid};
    report.detail="Publishing the verified copy";
    if(auto rc=io.Publish(report.staged_path,final,s.entries[0].directory))return rc;report.published=true;report.staged_path.clear();
    if(auto rc=io.Flush(destination_usb))return rc;
    if(move){report.detail="Checking both folders before removing the source";
        if(auto rc=Inventory(io,source_usb,s.root,current))return rc;if(!SameInventory(s,current))return R{IO::Invalid};
        Snapshot destination=s;destination.root=final;if(auto rc=Inventory(io,destination_usb,final,current))return rc;if(!SameInventory(destination,current))return R{IO::Invalid};
        report.detail="Removing verified source items";
        if(auto rc=RemoveReviewed(io,source_usb,s,report,buffer,true,destination_usb,final))return rc;
    }
    report.complete=true;report.detail=move?"Move complete. The destination was verified before the source was removed.":"Copy complete. The source was kept.";return R{};
}
template<class IO> auto Delete(IO& io,bool usb,const Snapshot& s,Report& report){
    using R=typename IO::Result;report={};report.detail="Checking the exact items reviewed for deletion";
    if(!ValidSnapshot(s))return R{IO::Invalid};std::vector<std::uint8_t> buffer(io.BufferSize());
    if(s.staging){Snapshot current;if(auto rc=Inventory(io,usb,s.root,current,true))return rc;if(!SameInventory(s,current))return R{IO::Invalid};}
    else if(auto rc=Recheck(io,usb,s,buffer))return rc;report.detail="Deleting reviewed items";
    if(auto rc=RemoveReviewed(io,usb,s,report,buffer))return rc;
    report.complete=true;report.detail="Deletion complete.";return R{};
}
}
