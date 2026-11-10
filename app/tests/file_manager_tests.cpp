// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the production engine against two separate, case-insensitive disks.
#include "nexus/file_manager_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <limits>

namespace fm=nexus::files;
using Bytes=std::vector<std::uint8_t>;
static unsigned checks{};
static void Check(bool value,const char* context="invariant"){
    ++checks;if(!value){std::cerr<<"FAIL "<<checks<<": "<<context<<'\n';std::exit(1);}
}
struct Node{
    bool directory{};Bytes data;
    std::uint64_t reported_size=UINT64_MAX;
    std::uint64_t Size()const{return directory?0:reported_size==UINT64_MAX?data.size():reported_size;}
    auto operator<=>(const Node&)const=default;
};
struct FatOrder{bool operator()(const std::string& a,const std::string& b)const{return fm::Fold(a)<fm::Fold(b);}};
using Tree=std::map<std::string,Node,FatOrder>;
static bool Under(const std::string& path,const std::string& root){return fm::Within(fm::Fold(path),fm::Fold(root));}
static Tree Subtree(const Tree& tree,const std::string& root){
    Tree result;for(const auto& [path,node]:tree)if(Under(path,root))result.emplace(path.substr(root.size()),node);return result;
}
struct Fake{
    using Result=std::uint32_t;
    static constexpr Result Invalid=10,Conflict=11,Fault=77,Changed=78,Cancelled=79,NoSpace=80;
    struct Hash{
        std::array<std::uint64_t,4> state{1469598103934665603ULL,1099511628211ULL,0x9e3779b97f4a7c15ULL,0x6a09e667f3bcc909ULL};
        void Update(const void* input,std::size_t n){
            const auto* p=static_cast<const std::uint8_t*>(input);
            for(std::size_t i=0;i<n;++i)for(auto& h:state){h^=p[i];h*=1099511628211ULL;}
        }
        fm::Digest Final(){fm::Digest d{};std::memcpy(d.data(),state.data(),d.size());return d;}
    };
    std::array<Tree,2> disks;
    bool source_usb{},destination_usb{true},deletion{},published{},no_space{},short_read{},corrupt_write{},dirty{};
    bool generation_changed{},cancelled{},stage_collision{};
    std::string root="/source",final="/out/copied",stage;
    fm::Snapshot snapshot;
    Tree original;
    std::size_t buffer_size=3,calls{},fail_at{},mutations{},fail_mutation{},reads{},writes{},removes{},flushes{},ends{},max_write{};
    bool fail_after{};
    std::vector<std::string> mutation_names;
    std::function<void(Fake&)> after_write,after_flush,after_publish,after_read,before_remove;
    std::function<void(const std::string&,std::vector<std::string>&)> list_override;
    Tree& Store(bool usb){return disks[usb];}
    const Tree& Store(bool usb)const{return disks[usb];}
    Result Step(){++calls;if(generation_changed)return Changed;if(cancelled)return Cancelled;return calls==fail_at?Fault:0;}
    Result Check(){return Step();}
    template<class Fn> Result Mutate(const std::string& name,Fn action){
        if(auto r=Step())return r;
        ++mutations;mutation_names.push_back(name);
        if(mutations==fail_mutation&&!fail_after)return Fault;
        if(auto r=action())return r;
        return mutations==fail_mutation&&fail_after?Fault:0;
    }
    std::size_t BufferSize(){return buffer_size;}
    void EndRead(){++ends;}
    void Progress(const char*,const std::string&,std::uint64_t done,std::uint64_t total){::Check(done<=total,"progress bounds");}
    Result Stat(bool usb,const std::string& path,fm::Entry& out,bool& exists){
        if(auto r=Step())return r;
        const auto found=Store(usb).find(path);exists=found!=Store(usb).end();
        if(exists)out={path,found->second.Size(),found->second.directory,{}};
        return 0;
    }
    Result List(bool usb,const std::string& path,std::vector<std::string>& out){
        if(auto r=Step())return r;
        const auto found=Store(usb).find(path);if(found==Store(usb).end()||!found->second.directory)return Invalid;
        out.clear();
        for(const auto& [name,node]:Store(usb)){
            (void)node;if(name!=path&&fm::Fold(fm::Parent(name))==fm::Fold(path))out.push_back(fm::Leaf(name));
        }
        if(list_override)list_override(path,out);
        return 0;
    }
    Result Read(bool usb,const std::string& path,std::uint64_t off,void* data,std::size_t count,std::uint64_t& got){
        if(auto r=Step())return r;
        auto it=Store(usb).find(path);if(it==Store(usb).end()||it->second.directory||off>it->second.data.size()||count>it->second.data.size()-off)return Invalid;
        got=count-(short_read&&count?1:0);if(got)std::memcpy(data,it->second.data.data()+off,got);++reads;
        if(after_read)after_read(*this);
        return 0;
    }
    Result Preflight(std::uint64_t required){if(auto r=Step())return r;::Check(required==snapshot.bytes,"preflight exact size");return no_space?NoSpace:0;}
    Result Add(bool usb,const std::string& path,Node node){
        const auto parent=Store(usb).find(fm::Parent(path));
        if(parent==Store(usb).end()||!parent->second.directory)return Invalid;
        return Store(usb).emplace(path,std::move(node)).second?0:Conflict;
    }
    Result NewStage(bool directory,std::uint64_t size,std::string& out){
        stage=fm::Child(fm::Parent(final),".nexus-fm-0123456789abcdef0123456789abcdef");
        if(stage_collision){Store(destination_usb).emplace(stage,Node{directory,{88}});return Conflict;}
        out=stage; // Retain an attempted name when creation may have happened.
        const auto rc=Mutate("new-stage",[&]{return Add(destination_usb,stage,{directory,Bytes(directory?0:size)});});
        return rc;
    }
    void Owned(bool usb,const std::string& path){::Check(usb==destination_usb&&Under(path,stage),"mutation stays on selected destination stage");}
    Result Directory(bool usb,const std::string& path){Owned(usb,path);return Mutate("directory",[&]{return Add(usb,path,{true,{}});});}
    Result Create(bool usb,const std::string& path,std::uint64_t size){Owned(usb,path);return Mutate("create",[&]{return Add(usb,path,{false,Bytes(size)});});}
    Result Write(bool usb,const std::string& path,std::uint64_t off,const void* data,std::size_t n){
        Owned(usb,path);return Mutate("write",[&]()->Result{
            auto it=Store(usb).find(path);if(it==Store(usb).end()||it->second.directory||off>it->second.data.size()||n>it->second.data.size()-off)return Invalid;
            std::memcpy(it->second.data.data()+off,data,n);if(corrupt_write&&n)it->second.data[off]^=1;
            ++writes;max_write=std::max(max_write,n);dirty=true;if(after_write)after_write(*this);return 0;
        });
    }
    Result Flush(bool usb){
        ::Check(usb==destination_usb||usb==source_usb,"flush explicit volume");
        return Mutate("flush",[&]()->Result{dirty=false;++flushes;if(after_flush)after_flush(*this);return 0;});
    }
    bool DestinationComplete()const{return Subtree(Store(destination_usb),final)==original;}
    Result Publish(const std::string& from,const std::string& to,bool directory){
        ::Check(from==stage&&to==final,"publish exact stage and destination");
        ::Check(!dirty,"flush before publication");
        ::Check(Subtree(Store(destination_usb),stage)==original,"whole stage matches reviewed data before publish");
        return Mutate("publish",[&]()->Result{
            auto& d=Store(destination_usb);if(d.contains(to))return Conflict;
            auto it=d.find(from);if(it==d.end()||it->second.directory!=directory)return Invalid;
            Tree moved;for(auto entry=d.begin();entry!=d.end();){
                if(Under(entry->first,from)){moved.emplace(to+entry->first.substr(from.size()),entry->second);entry=d.erase(entry);}else ++entry;
            }
            d.insert(moved.begin(),moved.end());published=true;
            if(after_publish)after_publish(*this);return 0;
        });
    }
    Result Remove(bool usb,const std::string& path,std::uint64_t size,bool directory){
        ::Check(usb==source_usb&&Under(path,root),"remove only exact reviewed source volume/tree");
        if(!deletion)::Check(published&&DestinationComplete(),"cut removal requires complete verified destination");
        return Mutate("remove",[&]()->Result{
            if(before_remove)before_remove(*this);
            auto& d=Store(usb);auto it=d.find(path);
            if(it==d.end()||it->second.directory!=directory||it->second.Size()!=size)return Invalid;
            if(directory)for(const auto& [name,node]:d){(void)node;if(name.size()>path.size()&&Under(name,path))return Invalid;}
            d.erase(it);++removes;return 0;
        });
    }
    void ResetCounters(){calls=mutations=reads=writes=removes=flushes=ends=max_write=0;mutation_names.clear();}
};
static Bytes Pattern(std::size_t size){Bytes data(size);for(std::size_t i=0;i<size;++i)data[i]=(i*31+i/11)%251;return data;}
static Fake Fixture(bool source=false,bool destination=true,int kind=0){
    Fake f;f.source_usb=source;f.destination_usb=destination;
    for(auto& disk:f.disks){disk["/"]={true,{}};disk["/out"]={true,{}};disk["/unrelated"]={false,{19,29,39}};}
    auto& src=f.Store(source);
    if(kind==1||kind==2)src[f.root]={false,kind==1?Pattern(17):Bytes{}};
    else{
        src[f.root]={true,{}};
        if(kind!=3){src[f.root+"/a.bin"]={false,Pattern(8)};src[f.root+"/sub"]={true,{}};
            src[f.root+"/sub/b.bin"]={false,Pattern(13)};src[f.root+"/sub/empty-dir"]={true,{}};src[f.root+"/zero"]={false,{}};}
    }
    ::Check(!fm::Prepare(f,source,f.root,f.snapshot),"prepare fixture");
    f.original=Subtree(src,f.root);f.ResetCounters();return f;
}
static Fake StageFixture(bool usb){
    auto f=Fixture(usb,!usb,1);f.root="/out/.nexus-fm-0123456789abcdef0123456789abcdef";f.deletion=true;
    f.Store(usb)[f.root]={true,{}};f.Store(usb)[f.root+"/part"]={false,{1,2,3}};f.Store(usb)[f.root+"/empty"]={true,{}};
    f.ResetCounters();::Check(!fm::Prepare(f,usb,f.root,f.snapshot,true));::Check(!f.reads);f.original=Subtree(f.Store(usb),f.root);f.ResetCounters();return f;
}
static void Untouched(const Fake& f){
    for(const auto& disk:f.disks)::Check(disk.at("/unrelated").data==Bytes({19,29,39}),"unrelated files untouched");
}
static void FailedTransfer(const Fake& f,const fm::Report& report){
    ::Check(!report.complete,"failure never marks complete");Untouched(f);
    if(!f.removes)::Check(Subtree(f.Store(f.source_usb),f.root)==f.original,"originals retained before removal");
    if(f.removes){::Check(f.published&&f.DestinationComplete(),"partial cut retains verified destination");::Check(report.removal_started,"uncertain removal reported truthfully");}
}
int main(){
    std::size_t injected{},post_mutation{};
    {auto good=Fixture();good.ResetCounters();fm::Snapshot s;::Check(!fm::Prepare(good,false,good.root,s));const auto calls=good.calls;
        for(std::size_t at=1;at<=calls;++at){auto f=Fixture();f.fail_at=at;fm::Snapshot bad;
            ::Check(fm::Prepare(f,false,f.root,bad)!=0,"preparation IO failure surfaced");
            ::Check(!f.mutations&&Subtree(f.Store(false),f.root)==f.original,"preparation remains read-only");++injected;}}
    for(bool source:{false,true})for(bool destination:{false,true})for(bool move:{false,true})for(int kind=0;kind<4;++kind){
        auto f=Fixture(source,destination,kind);fm::Report report;
        ::Check(!fm::Transfer(f,source,destination,f.snapshot,f.final,move,report),"transfer route/file/folder success");
        ::Check(report.complete&&report.published&&f.DestinationComplete(),"successful verified publication");
        ::Check(move?Subtree(f.Store(source),f.root).empty():Subtree(f.Store(source),f.root)==f.original,"copy versus cut source semantics");
        Untouched(f);
        if(kind!=0)continue;
        // Every IO boundary can fail, including cancellation checks and reads.
        const auto calls=f.calls,mutations=f.mutations;
        for(std::size_t at=1;at<=calls;++at){auto bad=Fixture(source,destination);bad.fail_at=at;fm::Report r;
            ::Check(fm::Transfer(bad,source,destination,bad.snapshot,bad.final,move,r)!=0,"injected IO failure surfaced");
            ::Check(bad.calls==at,"no further IO after a failed boundary");FailedTransfer(bad,r);++injected;
        }
        // IPC errors are also allowed after a mutation took effect. Never
        // conclude that a nonzero result means that the source is untouched.
        for(std::size_t at=1;at<=mutations;++at){auto bad=Fixture(source,destination);bad.fail_mutation=at;bad.fail_after=true;fm::Report r;
            ::Check(fm::Transfer(bad,source,destination,bad.snapshot,bad.final,move,r)!=0,"post-mutation failure surfaced");
            ::Check(bad.mutations==at,"no further mutation after uncertain result");FailedTransfer(bad,r);++post_mutation;
        }
    }
    {auto f=Fixture(false,true,1);f.Store(false)[f.root].data=Pattern(2*1024*1024+17);f.buffer_size=65536;
        ::Check(!fm::Prepare(f,false,f.root,f.snapshot));f.original=Subtree(f.Store(false),f.root);f.ResetCounters();fm::Report r;
        ::Check(!fm::Transfer(f,false,true,f.snapshot,f.final,true,r));::Check(f.max_write==65536&&f.writes==33);::Check(f.DestinationComplete());}
    for(bool move:{false,true})for(int mode=0;mode<7;++mode){auto f=Fixture();
        switch(mode){
        case 0:f.no_space=true;break;case 1:f.short_read=true;break;case 2:f.corrupt_write=true;break;case 3:f.buffer_size=0;break;
        case 4:f.after_write=[](Fake& x){x.generation_changed=true;};break;
        case 5:f.after_write=[](Fake& x){x.cancelled=true;};break;
        case 6:f.stage_collision=true;break;
        }
        fm::Report r;::Check(fm::Transfer(f,false,true,f.snapshot,f.final,move,r)!=0,"short/corrupt/space/cancel/generation/ownership reject");
        ::Check(!f.published&&!f.removes);FailedTransfer(f,r);
        if(mode==0||mode==3)::Check(!f.mutations,"preflight/buffer reject before stage");
    }
    for(bool same:{false,true})for(bool directory:{false,true}){
        auto f=Fixture(false,!same);f.Store(f.destination_usb)["/out/COPIED"]={directory,{42}};const auto old=f.disks;fm::Report r;
        ::Check(fm::Transfer(f,false,f.destination_usb,f.snapshot,f.final,true,r)==Fake::Conflict);::Check(f.disks==old&&f.mutations==0);
    }
    for(const char* final:{"/source","/SOURCE","/source/sub/new","/source/sub","/","/registered/new","/Nintendo/data","/switch/nexusmanager/new"}){
        auto f=Fixture(false,false);fm::Report r;::Check(fm::Transfer(f,false,false,f.snapshot,final,true,r)==Fake::Invalid);::Check(!f.mutations);}
    for(const char* path:{"/","/a/../b","/a//b","/a/","/a.","/a ","/a:b","/a\\b","/a?b","/CONFIG/NEXUSMANAGER/a","/switch/NEXUSMANAGER/a","/some/registered/a","/switch/nexusmanager.nro","/switch/.nexus-fm-other"})::Check(!fm::Safe(path),"path policy");
    ::Check(fm::Canonical("/caf\xc3\xa9"));::Check(fm::Safe("/My games/file.bin"));
    for(const char* path:{"/atmosphere/example.ini","/config/ordinary-app/settings.ini","/hbmenu.nro"})::Check(fm::Safe(path),"ordinary user-managed files remain available");
    for(const char* path:{"/bad\xc0\xaf","/bad\xed\xa0\x80","/bad\xf4\x90\x80\x80","/bad\xc3"})::Check(!fm::Canonical(path),"malformed UTF-8 rejected");
    ::Check(!fm::Canonical("/"+std::string(255,'x')));
    {auto f=Fixture();f.root="/Nintendo/data";fm::Snapshot s;::Check(fm::Prepare(f,false,f.root,s)==Fake::Invalid);::Check(!f.calls);}
    // Source mutations after review (same size and resized), and during copy,
    // must not be used to authorize deletion of the changed source.
    for(int mode=0;mode<4;++mode){auto f=Fixture();
        if(mode==0)f.Store(false)["/source/a.bin"].data[0]^=1;
        if(mode==1)f.Store(false)["/source/a.bin"].data.push_back(4);
        if(mode==2)f.after_write=[](Fake& x){x.Store(false)["/source/new.bin"]={false,{42}};};
        if(mode==3)f.after_publish=[](Fake& x){x.Store(false)["/source/a.bin"].data[0]^=1;};
        fm::Report r;::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(!f.removes);
        if(mode<3)::Check(!f.published);else ::Check(f.DestinationComplete());
    }
    {auto f=Fixture();f.after_flush=[](Fake& x){if(!x.published&&x.Store(true).contains(x.stage+"/a.bin"))x.Store(true).at(x.stage+"/a.bin").data[0]^=1;};
        fm::Report r;::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(!f.published&&!f.removes);}
    {auto f=Fixture();f.after_publish=[](Fake& x){x.Store(true).at(x.final+"/a.bin").data[0]^=1;};fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(f.published&&!f.removes);}
    {auto f=Fixture();f.after_flush=[](Fake& x){if(!x.published)x.Store(true)[x.stage+"/unreviewed"]={false,{44}};};fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(!f.published&&!f.removes,"extra staged file blocks publication");}
    {auto f=Fixture();f.after_flush=[](Fake& x){x.Store(true)[x.final]={false,{44}};};fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)==Fake::Conflict);::Check(!f.published&&!f.removes);
        ::Check(f.Store(true).at(f.final).data==Bytes{44},"late destination conflict never overwritten");}
    {auto f=Fixture();f.before_remove=[](Fake& x){if(!x.removes)x.Store(false).at("/source/sub/b.bin").data[0]^=1;};fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(f.removes==1&&r.removal_started);
        ::Check(f.DestinationComplete(),"partial cut retains whole reviewed destination");::Check(f.Store(false).contains("/source/sub/b.bin"));}
    // A late new child must survive: deletion is non-recursive even after the
    // original reviewed files have been removed.
    {auto f=Fixture();f.before_remove=[](Fake& x){x.Store(false)["/source/new.bin"]={false,{44}};};fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,f.final,true,r)!=0);::Check(f.Store(false).at("/source/new.bin").data==Bytes{44});
        ::Check(f.removes&&f.DestinationComplete());}
    for(bool usb:{false,true}){
        auto f=Fixture(usb,!usb);f.deletion=true;fm::Report r;::Check(!fm::Delete(f,usb,f.snapshot,r));::Check(r.complete&&Subtree(f.Store(usb),f.root).empty());Untouched(f);
        const auto calls=f.calls,mutations=f.mutations;
        for(std::size_t at=1;at<=calls;++at){auto bad=Fixture(usb,!usb);bad.deletion=true;bad.fail_at=at;fm::Report rr;
            ::Check(fm::Delete(bad,usb,bad.snapshot,rr)!=0);::Check(!rr.complete);if(bad.removes)::Check(rr.removal_started);Untouched(bad);++injected;}
        for(std::size_t at=1;at<=mutations;++at){auto bad=Fixture(usb,!usb);bad.deletion=true;bad.fail_mutation=at;bad.fail_after=true;fm::Report rr;
            ::Check(fm::Delete(bad,usb,bad.snapshot,rr)!=0);::Check(!rr.complete);if(bad.removes)::Check(rr.removal_started);Untouched(bad);++post_mutation;}
        for(int mode=0;mode<3;++mode){auto bad=Fixture(usb,!usb);bad.deletion=true;
            if(mode==0)bad.Store(usb)["/source/new"]={false,{7}};
            if(mode==1)bad.Store(usb)["/source/a.bin"].data[0]^=1;
            if(mode==2)bad.after_read=[](Fake& x){x.Store(x.source_usb)["/source/new"]={false,{7}};};
            fm::Report rr;::Check(fm::Delete(bad,usb,bad.snapshot,rr)!=0);::Check(!bad.removes,"changed delete tree retained");}
    }
    // Validate bounded inventory before allocating or accepting an unsafe tree.
    for(int mode=0;mode<5;++mode){auto f=Fixture();
        f.list_override=[mode](const std::string& path,std::vector<std::string>& names){if(path!="/source")return;
            if(mode==0)names={"a.bin","A.BIN"};if(mode==1)names={"../escape"};if(mode==2)names={""};
            if(mode==3)names.resize(fm::MaxEntries+1,"x");if(mode==4)names={"missing"};};
        fm::Snapshot s;::Check(fm::Inventory(f,false,f.root,s)==Fake::Invalid);::Check(!f.mutations);}
    for(const char* alias:{"/SOURCE","/Source"}){auto f=Fixture();fm::Snapshot s;
        ::Check(fm::Prepare(f,false,alias,s)==Fake::Invalid,"source case alias rejected by enumeration");::Check(!f.mutations);}
    for(const char* final:{"/OUT/copied","/missing/copied"}){auto f=Fixture();fm::Report r;
        ::Check(fm::Transfer(f,false,true,f.snapshot,final,true,r)==Fake::Invalid,"destination parent spelling/existence checked");::Check(!f.mutations);}
    {auto f=Fixture(false,true,1);const std::string name="/caf\xc3\xa9";
        f.Store(false).emplace(name,f.Store(false).at(f.root));f.Store(false).erase(f.root);f.root=name;
        ::Check(!fm::Prepare(f,false,f.root,f.snapshot),"UTF-8 exact enumerated name supported");f.original=Subtree(f.Store(false),f.root);fm::Report r;
        ::Check(!fm::Transfer(f,false,true,f.snapshot,f.final,true,r));::Check(f.DestinationComplete());}
    {auto f=Fixture(false,true,1);const std::string name="/\xc3\x84-app";
        f.Store(false).emplace(name,f.Store(false).at(f.root));fm::Snapshot s;
        ::Check(fm::Prepare(f,false,"/\xc3\xa4-app",s)==Fake::Invalid,"Unicode aliases not guessed");}
    {auto f=Fixture(false,true,1);f.Store(false)["/Long filename.bin"]={false,{1}};fm::Snapshot s;
        ::Check(fm::Prepare(f,false,"/LONGFI~1.BIN",s)==Fake::Invalid,"8.3 alias absent from enumeration rejected");}
    {auto f=Fixture(false,true,3);std::string path=f.root;for(unsigned i=0;i<=fm::MaxDepth;++i){path+="/d";f.Store(false)[path]={true,{}};}
        fm::Snapshot s;::Check(fm::Inventory(f,false,f.root,s)==Fake::Invalid);}
    {auto f=Fixture(false,true,3);std::string path=f.root;for(unsigned i=0;i<fm::MaxDepth;++i){path+="/d";f.Store(false)[path]={true,{}};}
        fm::Snapshot s;::Check(!fm::Inventory(f,false,f.root,s));::Check(s.entries.size()==fm::MaxDepth+1);}
    {auto f=Fixture(false,true,1);f.Store(false).at(f.root).reported_size=fm::MaxBytes+1;fm::Snapshot s;::Check(fm::Inventory(f,false,f.root,s)==Fake::Invalid);}
    {auto f=Fixture(false,true,1);f.Store(false).at(f.root).reported_size=fm::MaxBytes;fm::Snapshot s;::Check(!fm::Inventory(f,false,f.root,s));::Check(s.bytes==fm::MaxBytes);}
    {auto f=Fixture(false,true,3);for(std::size_t i=0;i<fm::MaxEntries;++i)f.Store(false)["/source/f"+std::to_string(i)]={false,{}};
        fm::Snapshot s;::Check(fm::Inventory(f,false,f.root,s)==Fake::Invalid);}
    {auto f=Fixture(false,true,3);for(std::size_t i=1;i<fm::MaxEntries;++i)f.Store(false)["/source/f"+std::to_string(i)]={false,{}};
        fm::Snapshot s;::Check(!fm::Inventory(f,false,f.root,s));::Check(s.entries.size()==fm::MaxEntries);}
    for(int mode=0;mode<5;++mode){auto f=Fixture();auto s=f.snapshot;
        if(mode==0)s.entries.clear();if(mode==1)++s.bytes;if(mode==2)s.entries[0].path="/wrong";
        if(mode==3)std::swap(s.entries[1],s.entries[2]);if(mode==4)s.root="/registered/protected";
        fm::Report r;::Check(fm::Delete(f,false,s,r)==Fake::Invalid);::Check(!f.calls&&!f.mutations,"invalid reviewed deletion denied before IO");}
    // Copy/cut cannot use a retained hidden stage, but a deliberately prepared
    // exact stage can be reviewed and deleted without touching other stages.
    {auto f=Fixture(false,false,1);const auto stage="/out/.nexus-fm-0123456789abcdef0123456789abcdef";
        f.Store(false)[stage]={true,{}};f.Store(false)[std::string(stage)+"/part"]={false,{1,2,3}};
        f.ResetCounters();
        fm::Snapshot s;::Check(fm::Prepare(f,false,stage,s)==Fake::Invalid);::Check(!fm::Prepare(f,false,stage,s,true));
        ::Check(!f.reads,"temporary cleanup preparation does not read preallocated payload");
        fm::Report r;::Check(fm::Transfer(f,false,false,s,"/out/restored",false,r)==Fake::Invalid);
        f.root=stage;f.deletion=true;::Check(!fm::Delete(f,false,s,r));::Check(!f.Store(false).contains(stage));::Check(f.Store(false).contains("/source"));
        ::Check(!f.reads,"temporary cleanup never hashes payload");}
    for(int mode=0;mode<4;++mode){auto f=Fixture(false,false,1);const std::string stage="/out/.nexus-fm-0123456789abcdef0123456789abcdef";
        f.Store(false)[stage]={true,{}};f.Store(false)[stage+"/part"]={false,{1,2,3}};f.ResetCounters();
        fm::Snapshot s;::Check(!fm::Prepare(f,false,stage,s,true));f.root=stage;f.deletion=true;
        if(mode==0)f.Store(false)[stage+"/part"].data.push_back(4);
        if(mode==1)f.Store(false)[stage+"/part"]={true,{}};
        if(mode==2)f.Store(false)[stage+"/new"]={false,{42}};
        // Stage payload bytes are intentionally not trusted or read. Only the
        // exact reviewed temporary inventory is discarded after confirmation.
        if(mode==3)f.Store(false)[stage+"/part"].data[0]^=1;
        fm::Report r;const auto rc=fm::Delete(f,false,s,r);
        ::Check(mode==3?rc==0:rc==Fake::Invalid);::Check(!f.reads);
        if(mode!=3)::Check(!f.removes,"changed temporary inventory retained");
        ::Check(f.Store(false).contains("/source"));}
    for(bool usb:{false,true}){auto good=StageFixture(usb);fm::Report r;::Check(!fm::Delete(good,usb,good.snapshot,r));::Check(!good.reads);
        const auto calls=good.calls,mutations=good.mutations;
        for(std::size_t at=1;at<=calls;++at){auto f=StageFixture(usb);f.fail_at=at;fm::Report bad;
            ::Check(fm::Delete(f,usb,f.snapshot,bad)!=0);::Check(!bad.complete&&!f.reads);if(f.removes)::Check(bad.removal_started);Untouched(f);++injected;}
        for(std::size_t at=1;at<=mutations;++at){auto f=StageFixture(usb);f.fail_mutation=at;f.fail_after=true;fm::Report bad;
            ::Check(fm::Delete(f,usb,f.snapshot,bad)!=0);::Check(!bad.complete&&!f.reads);if(f.removes)::Check(bad.removal_started);Untouched(f);++post_mutation;}
        auto changed=StageFixture(usb);changed.before_remove=[](Fake& f){f.Store(f.source_usb).at(f.root+"/part").data.push_back(8);};fm::Report bad;
        ::Check(fm::Delete(changed,usb,changed.snapshot,bad)!=0);::Check(!changed.removes&&!changed.reads,"late temporary size change stops exact remove");
    }
    std::cout<<"File manager production core: "<<checks<<" checks; "<<injected<<" IO/cancel failures; "<<post_mutation<<" post-mutation failures passed\n";
}
