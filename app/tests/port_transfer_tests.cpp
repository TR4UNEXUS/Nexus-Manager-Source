#include "nexus/port_transfer_core.hpp"
#include "nexus/port_files_plan.hpp"
#include "ui/port_library_layout.hpp"
#include "nexus/library.hpp"
#include "nexus/port_catalog.hpp"
#ifdef NDEBUG
#error "Run these tests with assertions enabled (-UNDEBUG)"
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <functional>
using namespace nexus::port;
struct Node{bool dir{};std::vector<std::uint8_t> data;auto operator<=>(const Node&)const=default;};
struct Fake {
    using Result=unsigned;static constexpr Result Invalid=10,Conflict=11;
    struct Hash{std::uint64_t h=1469598103934665603ULL;void Update(const void* p,size_t n){for(auto b=static_cast<const std::uint8_t*>(p);n--;){h^=*b++;h*=1099511628211ULL;}}Digest Final(){Digest d{};std::memcpy(d.data(),&h,sizeof(h));return d;}};
    Plan plan;std::map<std::string,Node> sd,usb;unsigned steps{},fail_at{};size_t removed{},writes{},installs{};
    std::uint64_t read_bytes{},written_bytes{};unsigned buffer_requests{},flushes{};
    std::vector<nexus::port::Progress> progress;bool dirty{};
    bool launcher{},corrupt{},short_read{},no_space{};std::function<void(Fake&)> installed,after_publish;
    std::map<std::string,Node>& Store(bool u){return u?usb:sd;}
    Result Check(){return ++steps==fail_at?77:0;}
    size_t BufferSize(){++buffer_requests;return 3;}
    void EndRead(){}
    void Progress(const nexus::port::Progress& p){
        assert(p.total==plan.bytes && p.copied<=p.total && p.reused<=p.total-p.copied);
        assert(p.copied==written_bytes);
        if(!progress.empty())assert(p.Percent()>=progress.back().Percent());
        if(p.phase!=Phase::Complete)assert(p.Percent()<100);
        else assert((!plan.create_launcher||launcher) && Complete());
        progress.push_back(p);
    }
    Result Stat(bool u,const std::string& p,Entry& e,bool& exists){if(auto r=Check())return r;auto it=Store(u).find(p);exists=it!=Store(u).end();if(exists)e={p,it->second.dir?0:it->second.data.size(),it->second.dir};return 0;}
    Result List(bool u,const std::string& p,std::vector<std::string>& names){if(auto r=Check())return r;names.clear();for(const auto& [path,n]:Store(u))if(path.starts_with(p+"/")&&path.find('/',p.size()+1)==path.npos)names.push_back(path.substr(p.size()+1));return 0;}
    Result Read(bool u,const std::string& p,std::uint64_t off,void* out,size_t n,std::uint64_t& got){if(auto r=Check())return r;auto it=Store(u).find(p);if(it==Store(u).end()||it->second.dir||off+n>it->second.data.size())return Invalid;got=short_read&&n?n-1:n;std::memcpy(out,it->second.data.data()+off,got);read_bytes+=got;return 0;}
    Result Directory(bool u,const std::string& p){if(auto r=Check())return r;auto& s=Store(u);if(s.contains(p))return s[p].dir?0:Conflict;s[p]={true,{}};return 0;}
    Result Preflight(std::uint64_t){if(auto r=Check())return r;return no_space?55:0;}
    Result CreateTemporary(size_t i,std::uint64_t size,std::string& path){if(auto r=Check())return r;path="/stage/"+std::to_string(i);Store(!plan.source_usb)[path]={false,std::vector<std::uint8_t>(size)};return 0;}
    Result WriteTemporary(const std::string& p,std::uint64_t off,const void* data,size_t n){if(auto r=Check())return r;++writes;written_bytes+=n;dirty=true;auto& dest=Store(!plan.source_usb).at(p).data;std::memcpy(dest.data()+off,data,n);if(corrupt&&n)dest[off]^=1;return 0;}
    Result Flush(){if(auto r=Check())return r;++flushes;dirty=false;return 0;}
    Result Publish(const std::string& from,const std::string& to){if(auto r=Check())return r;assert(!dirty);auto& s=Store(!plan.source_usb);if(s.contains(to))return Conflict;s.emplace(to,s.at(from));s.erase(from);if(after_publish)after_publish(*this);return 0;}
    bool Complete(){for(const auto& e:plan.entries){auto& s=Store(!plan.source_usb);auto it=s.find(plan.Destination(e.path));if(it==s.end()||it->second.dir!=e.directory)return false;if(!e.directory&&it->second.data!=original.at(plan.Source(e.path)).data)return false;}return true;}
    std::map<std::string,Node> original;
    Result InstallLauncher(){if(auto r=Check())return r;assert(Complete());launcher=true;++installs;if(installed)installed(*this);return 0;}
    Result RemoveSource(const std::string& p,std::uint64_t size){if(auto r=Check())return r;assert((!plan.create_launcher||launcher)&&Complete());auto& s=Store(plan.source_usb);assert(s.at(p).data.size()==size);s.erase(p);++removed;return 0;}
};
Fake Fixture(bool usb=false,bool move=false){
    Fake f;f.plan.source_usb=usb;f.plan.move=move;f.plan.nro="/switch/left4dead/wine-nx-runtime.nro";f.plan.roots={"/switch/left4dead"};
    auto& s=f.Store(usb);s["/switch/left4dead"]={true,{}};s["/switch/left4dead/drive_c"]={true,{}};
    s["/switch/left4dead/drive_c/Left 4 Dead"]={true,{}};s["/switch/left4dead/drive_c/Left 4 Dead/left4dead.exe"]={false,{1,2,3,4,5,6,7}};
    s[f.plan.nro]={false,{6,7,8,9}};s["/switch/left4dead/settings.json"]={false,{9,8}};s["/switch/left4dead/empty"]={false,{}};
    s["/switch/unrelated.nro"]={false,{42}};assert(!Inventory(f,f.plan));f.original=s;f.steps=0;return f;
}
int main(){
    {const auto left=FindKnownPort(0,"/switch/left4dead/wine-nx-runtime.nro");assert(left&&left->roots=="/switch/left4dead"&&left->address==1&&!left->blocked);
        const auto jazz=FindKnownPort(0,"/switch/Jazz2.nro");assert(jazz&&jazz->roots.find("/Games/Jazz2")!=jazz->roots.npos);
        std::set<std::uint64_t> ids;for(const auto& known:KnownPorts){assert(ids.insert(known.title).second);assert(known.address==1||known.address==3);}
        assert(std::count_if(KnownPorts.begin(),KnownPorts.end(),[](const auto& k){return k.blocked;})==2);
        assert(!FindKnownPort(0,"/switch/wine/wine-nx-runtime.nro"));
        for(const auto& k:KnownPorts)if(k.shared)assert(FindKnownPort(0,k.nro,k.args)==&k);
    }
    {Plan plan;assert(FilesPlan(false,"/switch/test/test.nro",{"/switch/test","/switch/test/data","/Games/Test"},"",plan));
        assert(!plan.application&&!plan.create_launcher&&plan.roots.size()==2&&plan.Destination(plan.nro)=="/switch/test/test.nro");
        assert(FilesPlan(true,"/homebrew/test/sdmc/ports/game/game.nro",{"/homebrew/test/sdmc/ports/game","/homebrew/test/sdmc/data/game"},"",plan));
        assert(plan.nro=="/ports/game/game.nro"&&plan.Source(plan.nro)=="/homebrew/test/sdmc/ports/game/game.nro"&&plan.roots[1]=="/data/game");
        assert(FilesPlan(false,"/ports/game/game.nro",{"/ports/game"},"/homebrew/test/sdmc",plan));
        assert(plan.Destination(plan.nro)=="/homebrew/test/sdmc/ports/game/game.nro");
        assert(!FilesPlan(false,"/switch/test.nro",{"/switch"},"",plan));
        assert(!FilesPlan(false,"/switch/test.nro",{"/atmosphere/contents/0100b00b51230000"},"",plan));
        assert(!FilesPlan(true,"/homebrew/test/sdmc/test/test.nro",{"/switch/other"},"",plan));
    }
    {using namespace nexus::portgrid;
        assert(std::string(Filter(0))=="SD card + USB drive");
        assert(Navigate(0,13,-6)==0&&Navigate(6,13,-6)==0&&Navigate(11,13,1)==12&&Start(12)==12);
        assert(Navigate(11,12,6)==11&&Navigate(0,0,1)==0);
        for(size_t n=0;n<PageSize;++n){const auto r=Card(n);assert(r.x>=54&&r.x+r.w<=1226&&r.y>=180&&r.y+r.h<=590);
            for(size_t i=0;i<n;++i){const auto a=Card(i);assert(a.x+a.w<=r.x||r.x+r.w<=a.x||a.y+a.h<=r.y||r.y+r.h<=a.y);}}
    }
    assert(DefaultRoot("/switch/left4dead/wine-nx-runtime.nro")=="/switch/left4dead");
    assert(DefaultRoot("/switch/game.nro")=="/switch/game.nro");
    assert(DefaultRoot("/ports/quake/bin/quake.nro")=="/ports/quake/bin");
    for(auto bad:{"/","/switch","/Nintendo/Contents/a","/atmosphere/x","/SWITCH/NEXUSMANAGER/x","/switch/a/../b","/switch/a?b","/switch/a.","/switch/a//b"})assert(!Safe(bad));
    assert(Safe("/switch/left4dead/drive_c/Left 4 Dead/left4dead.exe"));assert(!Within("/switch/ab","/switch/a"));
    for(bool usb:{false,true})for(bool move:{false,true})for(bool shortcut:{false,true}){
        auto f=Fixture(usb,move);f.plan.create_launcher=shortcut;Report r;assert(!Execute(f,f.plan,r));assert(r.phase==Phase::Complete&&f.launcher==shortcut&&f.Complete());
        assert(f.progress.back().copied==f.plan.bytes && f.progress.back().reused==0 && f.progress.back().Percent()==100);
        // Full destination readback remains mandatory; redundant pre-copy and
        // temporary-file hash passes must not return as the game grows.
        assert(f.read_bytes==f.plan.bytes*(move?6:2));
        assert(f.buffer_requests==1 && f.flushes==5);
        assert(f.Store(usb).contains("/switch/unrelated.nro"));assert(move?f.removed==4:f.Store(usb)==f.original);
        const auto count=f.steps;
        for(unsigned at=1;at<=count;++at){auto fault=Fixture(usb,move);fault.plan.create_launcher=shortcut;fault.fail_at=at;Report report;assert(Execute(fault,fault.plan,report));
            assert(fault.progress.back().phase!=Phase::Complete);
            if(shortcut&&!fault.launcher)assert(fault.Store(usb)==fault.original);
            if(fault.removed)assert((!shortcut||fault.launcher)&&fault.Complete());
        }
        assert(shortcut?f.installs==1:f.installs==0);
        assert(r.Message(move).find(shortcut?"new HOME shortcut was created":"HOME shortcuts were not changed")!=std::string::npos);
        std::cout<<(shortcut?"With launcher: ":"Files only: ")<<(usb?"USB -> SD":"SD -> USB")<<(move?" move":" copy")<<": "<<count<<" injected failure points passed\n";
    }
    {auto f=Fixture();auto& d=f.Store(true);d[f.plan.nro]={false,{6,7,0,9}};auto before=d;Report r;assert(Execute(f,f.plan,r)==Fake::Conflict);assert(d==before&&!f.writes&&!f.launcher);}
    {auto f=Fixture();f.usb=f.sd;Report r;assert(!Execute(f,f.plan,r));assert(r.reused==4&&!f.writes);
        assert(f.progress.back().reused==f.plan.bytes && !f.progress.back().copied);
        assert(f.progress.back().Bytes().find("Already present")!=std::string::npos);}
    {auto f=Fixture();f.usb[f.plan.nro]=f.sd.at(f.plan.nro);Report r;assert(!Execute(f,f.plan,r));
        assert(f.progress.back().reused==4 && f.progress.back().copied==f.plan.bytes-4);}
    {auto f=Fixture();for(auto& [path,n]:f.sd)if(!n.dir)n.data.clear();assert(!Inventory(f,f.plan));f.original=f.sd;
        Report r;assert(!Execute(f,f.plan,r));assert(!f.progress.back().copied && f.progress.back().Percent()==100);}
    {nexus::port::Progress p;p.total=5ULL*1024*1024*1024;p.copied=512*1024;
        assert(p.Bytes()=="Copied 0.5 MB / 5.0 GB");
        p.total=1ULL<<40;assert(p.Bytes().find("1.0 TB")!=std::string::npos);
        p.Start(Phase::Copying,1ULL<<40);p.done=1ULL<<39;assert(p.Percent()==32);}
    for(int mode=0;mode<3;++mode){auto f=Fixture(false,true);f.corrupt=mode==0;f.short_read=mode==1;f.no_space=mode==2;Report r;assert(Execute(f,f.plan,r));assert(f.sd==f.original&&!f.launcher&&!f.removed);}
    {auto f=Fixture(false,true);f.installed=[](Fake& x){x.sd.at(x.plan.nro).data[0]^=1;};Report r;assert(Execute(f,f.plan,r));assert(!f.removed);}
    {auto f=Fixture(false,true);f.installed=[](Fake& x){x.usb.at(x.plan.nro).data[0]^=1;};Report r;assert(Execute(f,f.plan,r));assert(!f.removed);}
    {auto f=Fixture();f.sd["/switch/left4dead/new"]={false,{2}};Report r;assert(Execute(f,f.plan,r));assert(!f.writes);}
    {auto f=Fixture();f.after_publish=[](Fake& x){x.sd["/switch/left4dead/new"]={false,{2}};};Report r;
        assert(Execute(f,f.plan,r));assert(!f.launcher && !f.removed);}
    {auto f=Fixture();f.sd["/switch/left4dead/SETTINGS.JSON"]={false,{1}};Plan p=f.plan;assert(Inventory(f,p));}
    {auto f=Fixture(false,true);f.plan.retained_roots=f.plan.roots;Report r;assert(!Execute(f,f.plan,r));assert(r.retained==4&&!r.removed&&f.sd==f.original);}
    {auto f=Fixture(true,true);f.plan.source_root="/homebrew/l4d/sdmc";auto prior=f.usb;f.usb.clear();for(auto [p,n]:prior)f.usb[f.plan.source_root+p]=n;
        assert(!Inventory(f,f.plan));f.original=f.usb;Report r;assert(!Execute(f,f.plan,r));assert(f.sd.contains(f.plan.nro));}
    std::cout<<"Port directory selection, profile mapping, conflicts, reuse, corruption, short reads, space, changed trees and source/destination mutation passed\n";
}
