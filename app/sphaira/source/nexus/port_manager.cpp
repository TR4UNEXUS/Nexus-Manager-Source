#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/port_manager.hpp"
#include "nexus/port_catalog.hpp"
#include "nexus/port_files_plan.hpp"
#include "nexus/homebrew.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/transfer.hpp"
#include "nexus/vsd_policy.hpp"
#include "nexus/library.hpp"
#include "fs.hpp"
#include "nro.hpp"
#include "owo.hpp"
#include "image.hpp"
#include "app.hpp"
#include "utils/utils.hpp"
#include <yyjson.h>
#include <minizip/unzip.h>
#include <cstring>
#include <memory>
#include <set>
namespace sphaira::nexusports {
namespace {
namespace core=nexus::ports;namespace f=nexus::files;
constexpr Result Bad=nexus::Invalid,Missing=MAKERESULT(2,1);
struct Info {bool exists{},directory{};u64 size{};};
std::string Digest(const void* p,size_t n){u8 h[32];sha256CalculateHash(h,p,n);return nexusapp::HexId(h);}
std::string Key(const Port& p){const auto s=std::string(p.usb?"usb:":"sd:")+(p.nro.empty()?std::to_string(p.application):p.nro);return Digest(s.data(),s.size()).substr(0,16);}
std::string ModRoot(const Port& p){return std::string(core::Store)+"/"+Key(p);}
std::string Profile(const Port& p){return "/switch/nexusmanager/ports/"+Key(p)+".json";}
std::string Random(){u8 b[8];randomGet(b,sizeof(b));return nexusapp::HexId(b,sizeof(b));}
class Disk {
    ui::ProgressBox* progress;fs::FsNativeSd sd;nexus::Client usb;
    FsDeviceOperator device{};bool opened{},provider{};std::array<u8,16> cid{};
public:
    bool is_usb;u64 gen;bool quiet_hash{};
    void TaskProgress(u64 done,u64 total){if(progress)progress->SetDetailedProgress(nexus::WorkPart(done,total,99),"Applying port mod"_nx,{},"Overall task progress"_nx);}
    Disk(ui::ProgressBox* p,bool u,u64 g):progress(p),is_usb(u),gen(g){}
    ~Disk(){if(opened)fsDeviceOperatorClose(&device);}
    Result Open(){
        std::vector<nexusapp::Item> ignored;u64 current{};R_TRY(nexusfiles::ListFiles(progress,is_usb,"/",ignored,current));
        R_UNLESS(!gen||gen==current,Bad);gen=current;
        Result rc{};const auto state=nexus::Detect(rc);R_UNLESS(state!=nexus::Provider::Unknown,R_FAILED(rc)?rc:Bad);provider=state==nexus::Provider::Present;
        if(!is_usb){R_TRY(sd.GetFsOpenResult());R_TRY(fsOpenDeviceOperator(&device));opened=true;R_TRY(fsDeviceOperatorGetSdCardCid(&device,cid.data(),cid.size(),cid.size()));}
        return Check();
    }
    Result Check(){
        if(progress)R_TRY(progress->ShouldExitResult());R_UNLESS(nexus::lifecycle::CanPoll()&&nexus::transfer::Active()==nexus::transfer::Mode::Off,nexus::Unavailable);
        if(is_usb)R_TRY(usb.Check(gen));
        if(provider){NexusVsdStatus s{};R_TRY(usb.VirtualSdStatus(s));R_UNLESS(!(s.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}
        if(opened){std::array<u8,16> now{};R_TRY(fsDeviceOperatorGetSdCardCid(&device,now.data(),now.size(),now.size()));R_UNLESS(now==cid,nexus::Unavailable);}
        return 0;
    }
    Result Mutation(){R_TRY(Check());return is_usb?nexus::CheckUsbMutationAllowed():0;}
    Result Stat(const std::string& path,Info& info){
        info={};R_UNLESS(f::Canonical(path),Bad);R_TRY(Check());
        if(is_usb){nexus::Info i{};auto rc=usb.Stat(gen,path,i);if(rc==MAKERESULT(140,205))return 0;R_TRY(rc);info={true,bool(i.directory),i.directory?0:i.size};}
        else {FsDirEntryType type{};auto rc=sd.GetEntryType(path,&type);if(rc==Missing)return 0;R_TRY(rc);R_UNLESS(type==FsDirEntryType_File||type==FsDirEntryType_Dir,Bad);info={true,type==FsDirEntryType_Dir,0};
            if(!info.directory){fs::File file;R_TRY(sd.OpenFile(path,FsOpenMode_Read,&file));s64 size{};R_TRY(file.GetSize(&size));R_UNLESS(size>=0,Bad);info.size=size;}}
        return 0;
    }
    Result List(const std::string& path,std::vector<nexusapp::Item>& out){u64 current{};R_TRY(nexusfiles::ListFiles(progress,is_usb,path,out,current));R_UNLESS(current==gen,Bad);return Check();}
    Result Read(const std::string& path,u64 off,void* data,size_t size,u64& got){R_TRY(Check());R_UNLESS(f::Canonical(path),Bad);
        if(is_usb)return usb.Read(gen,path,off,data,size,got);fs::File file;R_TRY(sd.OpenFile(path,FsOpenMode_Read,&file));return file.Read(off,data,size,FsReadOption_None,&got);}
    Result Parents(const std::string& path){
        R_UNLESS(f::Canonical(path),Bad);const auto parent=f::Parent(path);
        for(size_t at=parent.find('/',1);;at=parent.find('/',at+1)){const auto part=parent.substr(0,at);Info i;R_TRY(Stat(part,i));
            if(i.exists){R_UNLESS(i.directory,Bad);}else{R_TRY(Mutation());R_TRY(is_usb?usb.CreateDirectory(gen,part):sd.CreateDirectory(part));}if(at==parent.npos)break;}
        return 0;
    }
    Result Space(u64 bytes){R_TRY(Check());nexus::Space space{};if(is_usb){R_TRY(usb.GetSpace(gen,space));}else{s64 available{};R_TRY(sd.GetFreeSpace("/",&available));R_UNLESS(available>=0,Bad);space.free=available;}R_UNLESS(bytes<=space.free&&space.free-bytes>=32*1024*1024,MAKERESULT(2,30));return 0;}
    Result Create(const std::string& path,u64 size){R_TRY(Parents(path));Info i;R_TRY(Stat(path,i));R_UNLESS(!i.exists,Bad);R_TRY(Mutation());return is_usb?usb.CreateFile(gen,path,size):sd.CreateFile(path,size,size>=0x100000000ULL?FsCreateOption_BigFile:0);}
    Result Write(const std::string& path,u64 off,const void* data,size_t size){R_TRY(Mutation());
        if(is_usb)return usb.Write(gen,path,off,data,size);fs::File file;R_TRY(sd.OpenFile(path,FsOpenMode_Write,&file));return file.Write(off,data,size,FsWriteOption_Flush);}
    Result Flush(){R_TRY(Mutation());return is_usb?usb.Flush(gen):sd.Commit();}
    Result Rename(const std::string& from,const std::string& to){R_UNLESS(f::Canonical(from)&&f::Canonical(to),Bad);Info a,b;R_TRY(Stat(from,a));R_TRY(Stat(to,b));R_UNLESS(a.exists&&!b.exists,Bad);R_TRY(Parents(to));R_TRY(Mutation());
        if(is_usb){R_TRY(usb.RenameNew(gen,from,to));}else{R_TRY(a.directory?sd.RenameDirectory(from,to):sd.RenameFile(from,to));}return Flush();}
    Result EraseOwned(const std::string& path){R_UNLESS(f::Within(path,std::string(core::Store))||path.starts_with("/switch/nexusmanager/ports/"),Bad);Info i;R_TRY(Stat(path,i));if(!i.exists)return 0;R_UNLESS(!i.directory,Bad);R_TRY(Mutation());
        R_TRY(is_usb?usb.DeleteFileManagerEntry(gen,path,i.size,false,path):sd.DeleteFile(path));return Flush();}
    Result Text(const std::string& path,std::string& out){Info i;R_TRY(Stat(path,i));R_UNLESS(i.exists&&!i.directory&&i.size<=4*1024*1024,Bad);out.resize(i.size);u64 got{};R_TRY(Read(path,0,out.data(),out.size(),got));R_UNLESS(got==out.size(),Bad);return 0;}
    Result NewText(const std::string& path,const std::string& text){R_TRY(Create(path,text.size()));R_TRY(Write(path,0,text.data(),text.size()));R_TRY(Flush());std::string read;R_TRY(Text(path,read));R_UNLESS(read==text,Bad);return 0;}
    Result Hash(const std::string& path,u64 size,std::string& out){Info i;R_TRY(Stat(path,i));R_UNLESS(i.exists&&!i.directory&&i.size==size,Bad);Sha256Context sha;sha256ContextCreate(&sha);std::vector<u8> buffer(256*1024);
        if(progress&&!quiet_hash)progress->NewTransfer(path);
        for(u64 off=0;off<size;){const auto n=std::min<u64>(buffer.size(),size-off);u64 got{};R_TRY(Read(path,off,buffer.data(),n,got));R_UNLESS(got==n,Bad);sha256ContextUpdate(&sha,buffer.data(),n);off+=n;if(progress&&!quiet_hash)progress->UpdateTransferBytes(off,size);}
        u8 hash[32];sha256ContextGetHash(&sha,hash);out=nexusapp::HexId(hash);R_TRY(Stat(path,i));R_UNLESS(i.exists&&!i.directory&&i.size==size,Bad);return Check();}
};
using Json=std::unique_ptr<yyjson_doc,decltype(&yyjson_doc_free)>;
Json Parse(const std::string& text){return Json(yyjson_read(text.data(),text.size(),0),yyjson_doc_free);}
std::string Str(yyjson_val* o,const char* key){auto* v=yyjson_obj_get(o,key);return yyjson_is_str(v)?std::string(yyjson_get_str(v),yyjson_get_len(v)):std::string{};}
u64 Num(yyjson_val* o,const char* key){return yyjson_get_uint(yyjson_obj_get(o,key));}
std::string WriteJson(yyjson_mut_doc* doc){size_t n{};char* raw=yyjson_mut_write(doc,YYJSON_WRITE_PRETTY,&n);std::string result=raw?std::string(raw,n):std::string{};free(raw);yyjson_mut_doc_free(doc);return result;}
std::string Encode(const core::ModPlan& p){auto* d=yyjson_mut_doc_new(nullptr);auto* o=yyjson_mut_obj(d);yyjson_mut_doc_set_root(d,o);
    yyjson_mut_obj_add_strcpy(d,o,"root",p.root.c_str());yyjson_mut_obj_add_strcpy(d,o,"store",p.store.c_str());yyjson_mut_obj_add_strcpy(d,o,"name",p.name.c_str());yyjson_mut_obj_add_uint(d,o,"bytes",p.bytes);
    auto* rows=yyjson_mut_arr(d);yyjson_mut_obj_add_val(d,o,"files",rows);
    for(const auto& f:p.files){auto* r=yyjson_mut_obj(d);yyjson_mut_arr_add_val(rows,r);yyjson_mut_obj_add_strcpy(d,r,"path",f.relative.c_str());yyjson_mut_obj_add_strcpy(d,r,"hash",f.hash.c_str());yyjson_mut_obj_add_uint(d,r,"size",f.size);yyjson_mut_obj_add_bool(d,r,"had_original",f.had_original);yyjson_mut_obj_add_strcpy(d,r,"original_hash",f.original_hash.c_str());yyjson_mut_obj_add_uint(d,r,"original_size",f.original_size);}return WriteJson(d);}
bool Decode(const std::string& text,core::ModPlan& p){p={};auto d=Parse(text);if(!d)return false;auto* o=yyjson_doc_get_root(d.get());p.root=Str(o,"root");p.store=Str(o,"store");p.name=Str(o,"name");p.bytes=Num(o,"bytes");auto* rows=yyjson_obj_get(o,"files");if(!yyjson_is_arr(rows)||yyjson_arr_size(rows)>core::MaxModFiles)return false;
    size_t i,n;yyjson_val* r;yyjson_arr_foreach(rows,i,n,r){p.files.push_back({Str(r,"path"),Str(r,"hash"),Num(r,"size"),yyjson_get_bool(yyjson_obj_get(r,"had_original")),Str(r,"original_hash"),Num(r,"original_size")});}return core::Valid(p);}
Result SaveProfile(ui::ProgressBox* p,const Port& port){Disk sd(p,false,0);R_TRY(sd.Open());auto* d=yyjson_mut_doc_new(nullptr);auto* o=yyjson_mut_obj(d);yyjson_mut_doc_set_root(d,o);yyjson_mut_obj_add_strcpy(d,o,"nro",port.nro.c_str());yyjson_mut_obj_add_strcpy(d,o,"name",port.name.c_str());yyjson_mut_obj_add_bool(d,o,"usb",port.usb);yyjson_mut_obj_add_uint(d,o,"application",port.application);yyjson_mut_obj_add_strcpy(d,o,"args",port.args.c_str());yyjson_mut_obj_add_uint(d,o,"address",port.address);
    auto* roots=yyjson_mut_arr(d);yyjson_mut_obj_add_val(d,o,"roots",roots);for(const auto& root:port.roots)yyjson_mut_arr_add_strcpy(d,roots,root.c_str());
    const auto path=Profile(port),temp=path+".new-"+Random(),previous=path+".previous";R_TRY(sd.NewText(temp,WriteJson(d)));Info i;R_TRY(sd.Stat(previous,i));if(i.exists)R_TRY(sd.EraseOwned(previous));R_TRY(sd.Stat(path,i));if(i.exists)R_TRY(sd.Rename(path,previous));R_TRY(sd.Rename(temp,path));return 0;}
Result LoadProfile(ui::ProgressBox* p,Port& port,bool prefer=false){Disk sd(p,false,0);R_TRY(sd.Open());Info i;auto path=Profile(port);R_TRY(sd.Stat(path,i));if(!i.exists){path+=".previous";R_TRY(sd.Stat(path,i));if(!i.exists)return 0;}
    std::string text;R_TRY(sd.Text(path,text));auto doc=Parse(text);R_UNLESS(doc,Bad);auto* o=yyjson_doc_get_root(doc.get());R_UNLESS(Str(o,"nro")==port.nro,Bad);if(!port.application||prefer)port.application=Num(o,"application");if(port.args.empty())port.args=Str(o,"args");const auto address=Num(o,"address");if(address==1||address==3)port.address=address;
    auto* roots=yyjson_obj_get(o,"roots");R_UNLESS(yyjson_is_arr(roots)&&yyjson_arr_size(roots)<=16,Bad);port.roots.clear();size_t at,n;yyjson_val* v;yyjson_arr_foreach(roots,at,n,v){R_UNLESS(yyjson_is_str(v),Bad);std::string root=yyjson_get_str(v);R_UNLESS(core::Root(root),Bad);if(std::find(port.roots.begin(),port.roots.end(),root)==port.roots.end())port.roots.push_back(root);}return 0;}
void Defaults(Port& port){if(!port.nro.empty()){const auto root=core::DefaultRoot(port.nro);if(!root.empty())port.roots.push_back(root);
    if(const auto* known=nexus::port::FindKnownPort(port.application,port.nro);known&&!known->shared&&!known->blocked){for(size_t at=0;at<known->roots.size();){auto end=known->roots.find('\n',at);if(end==known->roots.npos)end=known->roots.size();std::string root(known->roots.substr(at,end-at));if(core::Root(root)&&std::find(port.roots.begin(),port.roots.end(),root)==port.roots.end())port.roots.push_back(root);at=end+1;}}
    }if(port.application==0x0100b00b51230000ULL)port.roots.push_back("/atmosphere/contents/0100b00b51230000");
    std::sort(port.roots.begin(),port.roots.end());port.roots.erase(std::unique(port.roots.begin(),port.roots.end()),port.roots.end());}
Result GuardPort(const Port& port){
    R_UNLESS(!App::IsApplet(),nexus::Unavailable);
    u64 running{};R_TRY(svcGetInfo(&running,InfoType_ProgramId,CUR_PROCESS_HANDLE,0));R_UNLESS(!port.application||port.application!=running,Bad);
    if(!envIsNso()){auto active=std::string(App::GetExePath());if(active.starts_with("sdmc:"))active.erase(0,5);R_UNLESS(f::Canonical(active),Bad);if(!port.usb)for(const auto& root:port.roots)R_UNLESS(!core::Overlap(root,active),Bad);}
    return 0;
}
bool Allowed(const Port& port,const core::ModPlan& plan){return core::Valid(plan)&&f::Parent(plan.store)==ModRoot(port)&&core::Hex(f::Leaf(plan.store),16)&&std::find(port.roots.begin(),port.roots.end(),plan.root)!=port.roots.end();}
Result LoadMod(Disk& disk,const Port& port,const Mod& mod,const char* file,core::ModPlan& plan){R_UNLESS(f::Parent(mod.path)==ModRoot(port)&&core::Hex(f::Leaf(mod.path),16),Bad);std::string text;R_TRY(disk.Text(mod.path+"/"+file,text));R_UNLESS(Decode(text,plan)&&plan.store==mod.path&&Allowed(port,plan),Bad);return 0;}
class ModIO {
    Disk& disk;const core::ModPlan& plan;std::string& detail;
public:
    Result rc{};ModIO(Disk& d,const core::ModPlan& p,std::string& text):disk(d),plan(p),detail(text){disk.quiet_hash=true;}
    ~ModIO(){disk.quiet_hash=false;}
    bool Take(Result r){if(R_FAILED(r)){rc=r;detail="The mod operation stopped. Use Disable / recover to restore its original files. Changed or missing files are kept for review."_nx;return false;}return true;}
    bool Fail(const std::string& s){rc=Bad;detail=s;return false;}
    void Progress(size_t done,size_t total){disk.TaskProgress(done,total);}
    bool Check(){return Take(disk.Check());}
    bool Exists(const std::string& path,bool& out){Info i;if(!Take(disk.Stat(path,i)))return false;if(i.exists&&i.directory)return Fail("A folder conflicts with a mod file. Nothing was replaced."_nx);out=i.exists;return true;}
    bool Match(const std::string& path,bool wanted,u64 size,const std::string& hash){Info i;if(!Take(disk.Stat(path,i)))return false;if(i.exists!=wanted||i.directory||(wanted&&i.size!=size))return Fail("A mod or original file changed: "_nx+path);if(!wanted)return true;std::string got;return Take(disk.Hash(path,size,got))&&(got==hash||Fail("File content changed: "_nx+path));}
    bool Parents(const std::string& path){return Take(disk.Parents(path));}
    bool Rename(const std::string& a,const std::string& b){return Take(disk.Rename(a,b));}
    bool Flush(){return Take(disk.Flush());}
    bool Begin(const core::ModPlan& p){return Take(disk.NewText(p.store+"/session.json",Encode(p)));}
    bool Activated(){return Take(disk.NewText(plan.store+"/active","1"));}
    bool Finished(){return Take(disk.EraseOwned(plan.store+"/active"))&&Take(disk.EraseOwned(plan.store+"/session.json"));}
};
}
Result Scan(ui::ProgressBox* p,bool usb,std::vector<Port>& ports,std::string& detail){
    ports.clear();Disk disk(p,usb,0);R_TRY(disk.Open());std::vector<nexusapp::Game> installed;R_TRY(nexusapp::ScanGames(p,installed));std::set<u64> titles;for(const auto& g:installed)titles.insert(g.id);
    std::vector<std::pair<std::string,unsigned>> pending{{"/switch",0}};std::set<std::string> paths;size_t seen{};
    for(size_t at=0;at<pending.size();++at){const auto [path,depth]=pending[at];Info info;R_TRY(disk.Stat(path,info));if(!info.exists)continue;R_UNLESS(info.directory,Bad);std::vector<nexusapp::Item> items;R_TRY(disk.List(path,items));
        for(const auto& item:items){R_UNLESS(++seen<=16384,Bad);if(!core::Root(item.path))continue;
            if(item.directory){if(depth<1)pending.emplace_back(item.path,depth+1);continue;}
            if(!nexus_vsd::Nro(item.path)||!paths.insert(item.path).second)continue;
            Port port;port.usb=usb;port.generation=disk.gen;port.nro=item.path;port.name=f::Leaf(f::Parent(item.path));if(f::Parent(item.path)=="/switch")port.name=item.name;
            if(const auto* known=nexus::port::FindKnownPort(0,item.path)){port.name=known->name;port.args=known->args;port.address=known->address;if(titles.contains(known->title))port.application=known->title;}
            if(usb){const auto identity="USB Loader VSD v1:"_nx+port.nro;u8 h[32];sha256CalculateHash(h,identity.data(),identity.size());u64 id{};memcpy(&id,h,8);id=0x0500000000000000ULL|(id&0x00FFFFFFFFFFF000ULL);if(titles.contains(id))port.application=id;}
            Defaults(port);R_TRY(LoadProfile(p,port,true));ports.push_back(std::move(port));
        }
    }
    Info gta;R_TRY(disk.Stat("/atmosphere/contents/0100b00b51230000",gta));if(gta.exists&&gta.directory){Port port{"Grand Theft Auto V"_nx,{},usb,disk.gen,0x0100b00b51230000ULL,{}};Defaults(port);R_TRY(LoadProfile(p,port,true));ports.push_back(std::move(port));}
    Disk profiles(p,false,0);R_TRY(profiles.Open());Info profiles_info;R_TRY(profiles.Stat("/switch/nexusmanager/ports",profiles_info));
    if(profiles_info.exists){std::vector<nexusapp::Item> saved;R_TRY(profiles.List("/switch/nexusmanager/ports",saved));R_UNLESS(saved.size()<=1024,Bad);
        for(const auto& file:saved){if(file.directory||!file.name.ends_with(".json"))continue;std::string text;R_TRY(profiles.Text(file.path,text));auto doc=Parse(text);R_UNLESS(doc,Bad);auto* o=yyjson_doc_get_root(doc.get());if(yyjson_get_bool(yyjson_obj_get(o,"usb"))!=usb)continue;
            Port extra;extra.name=Str(o,"name");extra.nro=Str(o,"nro");extra.application=Num(o,"application");extra.usb=usb;extra.generation=disk.gen;
            R_UNLESS(Profile(extra)==file.path&&((core::Root(extra.nro)&&nexus_vsd::Nro(extra.nro))||(extra.nro.empty()&&extra.application==0x0100b00b51230000ULL)),Bad);
            if(std::any_of(ports.begin(),ports.end(),[&](const auto& existing){return Key(existing)==Key(extra);}))continue;
            if(!extra.nro.empty()){Info present;R_TRY(disk.Stat(extra.nro,present));if(!present.exists||present.directory)continue;}
            Defaults(extra);R_TRY(LoadProfile(p,extra,true));ports.push_back(std::move(extra));}}
    std::sort(ports.begin(),ports.end(),[](const auto& a,const auto& b){return f::Fold(a.name)<f::Fold(b.name);});detail="Choose a port to manage its folders, mods and HOME shortcut. Add NRO finds apps outside the usual folders."_nx;return 0;
}
Result FromGame(ui::ProgressBox* p,const nexusapp::Game& game,Port& port,std::string& detail){
    port={};port.name=game.name;port.application=game.id;port.usb=game.usb_forwarder;port.args=game.forwarder_args;port.address=game.forwarder_address_space;
    port.nro=game.usb_forwarder?game.forwarder_usb_path:game.forwarder_path;if(port.nro.starts_with("sdmc:"))port.nro.erase(0,5);
    R_UNLESS((!port.nro.empty()&&core::Root(port.nro))||port.application==0x0100b00b51230000ULL,Bad);
    Disk disk(p,port.usb,0);R_TRY(disk.Open());port.generation=disk.gen;Defaults(port);R_TRY(LoadProfile(p,port));detail="Manage this port's files separately from its HOME shortcut."_nx;return 0;
}
Result RememberPort(ui::ProgressBox* p,const Port& port){R_UNLESS(core::Root(port.nro)&&nexus_vsd::Nro(port.nro)&&!port.roots.empty(),Bad);return SaveProfile(p,port);}
Result ReadIcon(const Port& port,std::vector<u8>& icon){
    icon.clear();R_UNLESS(core::Root(port.nro)&&nexus_vsd::Nro(port.nro),Bad);
    Disk disk(nullptr,port.usb,port.generation);R_TRY(disk.Open());Info info;R_TRY(disk.Stat(port.nro,info));R_UNLESS(info.exists&&!info.directory,Bad);
    auto read=[&](u64 off,void* data,size_t size)->Result{u64 got{};R_TRY(disk.Read(port.nro,off,data,size,got));R_UNLESS(got==size,Bad);return 0;};
    std::array<u8,128> header{};R_TRY(read(0,header.data(),header.size()));nexus::library::Assets assets{};R_UNLESS(nexus::library::Header(header,info.size,assets),Bad);
    std::array<u8,56> aset{};R_UNLESS(nexus::library::Range(assets.code,aset.size(),info.size),Bad);R_TRY(read(assets.code,aset.data(),aset.size()));
    R_UNLESS(nexus::library::AssetHeader(aset,info.size,assets)&&assets.icon_size&&assets.icon_size<=1024*1024,Bad);
    icon.resize(assets.icon_size);R_TRY(read(assets.icon,icon.data(),icon.size()));return disk.Check();
}
Result PrepareTransfer(ui::ProgressBox* p,const Port& port,bool move,nexus::port::Plan& plan,std::string& detail){
    plan={};detail="Checking the selected port files"_nx;R_TRY(GuardPort(port));
    R_UNLESS(!port.nro.empty(),Bad);Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());
    std::vector<Mod> mods;R_TRY(ListMods(p,port,mods,detail));for(const auto& mod:mods)if(mod.active||mod.pending){detail="Disable / recover port mods before transferring its files."_nx;return Bad;}
    std::string destination_root;
    if(!port.usb&&!port.nro.starts_with("/switch/")){const auto id=port.nro+"\n"+port.args;destination_root="/homebrew/port-"+Digest(id.data(),id.size()).substr(0,16)+"/sdmc";}
    detail="Choose a port NRO and its own data folders. Atmosphere content needs its existing firmware mapping."_nx;
    R_UNLESS(nexus::port::FilesPlan(port.usb,port.nro,port.roots,destination_root,plan),Bad);
    plan.name=port.name;plan.args=port.args;plan.address_space=port.address;plan.move=move;
    nexus::Client client;nexus::Status state{};R_TRY(client.Query(state));R_UNLESS(state.Available(),nexus::Unavailable);
    R_UNLESS(!port.usb||port.generation==state.generation,nexus::Unavailable);plan.generation=state.generation;
    // Shared folders may be copied, but moving one port must not delete another.
    std::vector<Port> all;R_TRY(Scan(p,port.usb,all,detail));
    for(const auto& root:plan.roots){bool shared=false;
        for(const auto& other:all)if(Key(other)!=Key(port))for(const auto& owned:other.roots)shared|=core::Overlap(plan.Source(root),owned);
        for(const auto& known:nexus::port::KnownPorts)if(known.shared&&known.nro==plan.nro)shared=true;
        if(shared)plan.retained_roots.push_back(root);
    }
    return nexusapp::InspectPortFiles(p,plan,detail);
}
Result Transfer(ui::ProgressBox* p,const Port& port,const nexus::port::Plan& plan,nexus::port::Report& report,Port& destination){
    report={};nexus::port::Plan fresh;std::string detail;const auto prepared=PrepareTransfer(p,port,plan.move,fresh,detail);
    if(R_FAILED(prepared)){report.detail=detail;return prepared;}
    R_UNLESS(!plan.create_launcher&&!plan.application&&fresh.generation==plan.generation&&fresh.source_usb==plan.source_usb&&
        fresh.nro==plan.nro&&fresh.args==plan.args&&fresh.roots==plan.roots&&fresh.retained_roots==plan.retained_roots&&
        fresh.source_root==plan.source_root&&fresh.destination_root==plan.destination_root&&fresh.entries==plan.entries&&fresh.bytes==plan.bytes,Bad);
    R_TRY(nexusapp::TransferPort(p,fresh,report));
    destination=port;destination.usb=!port.usb;destination.application=0;destination.nro=plan.Destination(plan.nro);destination.roots.clear();
    for(const auto& root:plan.roots)destination.roots.push_back(plan.Destination(root));
    Disk target(p,destination.usb,0);R_TRY(target.Open());destination.generation=target.gen;
    const auto saved=SaveProfile(p,destination);if(R_FAILED(saved))report.detail="Files transferred, but the destination's saved folder links could not be updated. Use Add NRO to open it again."_nx;
    return saved;
}
Result PrepareShortcutRemoval(ui::ProgressBox* p,const Port& port,nexus::Target target,nexusapp::GameRemovalPlan& plan){
    plan={};plan.detail="Could not confirm that this HOME application launches the selected port. Its files were kept."_nx;
    R_UNLESS(port.application,Bad);nexusapp::Game current;R_TRY(nexusapp::InspectGame(p,port.application,current));
    if(port.application!=0x0100b00b51230000ULL){auto path=current.usb_forwarder?current.forwarder_usb_path:current.forwarder_path;if(path.starts_with("sdmc:"))path.erase(0,5);
        R_UNLESS(!path.empty()&&path==port.nro&&current.usb_forwarder==port.usb&&current.forwarder_args==port.args,Bad);}
    nexusapp::CleanupItem item;item.kind=nexusapp::CleanupKind::Game;item.target=target;item.application=port.application;item.name=port.name;return nexusapp::PreviewGameRemoval(p,item,plan);
}
Result LinkFolder(ui::ProgressBox* p,Port& port,const nexusfiles::Selection& selected,std::string& detail){
    detail="Choose a specific port folder on the same storage. System folders and shared storage roots cannot be linked."_nx;
    R_UNLESS(selected.usb==port.usb&&selected.generation==port.generation&&selected.item.directory&&core::Root(selected.item.path)&&port.roots.size()<16,Bad);
    for(const auto& root:port.roots)R_UNLESS(!core::Overlap(root,selected.item.path),Bad);
    Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());Info i;R_TRY(disk.Stat(selected.item.path,i));R_UNLESS(i.exists&&i.directory,Bad);
    auto next=port;next.roots.push_back(selected.item.path);R_TRY(SaveProfile(p,next));port=std::move(next);detail="Folder linked. It will appear in the deletion review and as a mod destination."_nx;return 0;
}
Result UnlinkFolder(ui::ProgressBox* p,Port& port,const std::string& root,std::string& detail){
    detail="Keep at least one port folder linked."_nx;R_UNLESS(port.roots.size()>1,Bad);std::vector<Mod> mods;R_TRY(ListMods(p,port,mods,detail));for(const auto& mod:mods)if(mod.root==root){detail="Disable and delete imported mods for this folder before unlinking it. Their original-file backups must remain reachable."_nx;return Bad;}auto next=port;const auto n=std::erase(next.roots,root);R_UNLESS(n==1,Bad);R_TRY(SaveProfile(p,next));port=std::move(next);detail="Folder unlinked. Its files were not changed."_nx;return 0;
}
Result PrepareRemoval(ui::ProgressBox* p,const Port& port,Removal& removal,std::string& detail){
    R_TRY(GuardPort(port));removal={};removal.port=port;detail="Checking this port's folders. Nothing has been deleted."_nx;R_UNLESS(!port.roots.empty(),Bad);
    Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());std::vector<Mod> mods;R_TRY(ListMods(p,port,mods,detail));
    for(const auto& mod:mods)if(mod.active||mod.pending){detail="Disable / recover this port's mods before deleting its files. Original-file backups must be restored first."_nx;return Bad;}
    std::vector<Port> all;R_TRY(Scan(p,port.usb,all,detail));
    for(const auto& root:port.roots){R_UNLESS(core::Root(root),Bad);
        for(const auto& other:all)if(Key(other)!=Key(port)){for(const auto& owned:other.roots)if(core::Overlap(root,owned)){detail="This folder is shared with "_nx+other.name+": "+root+". Manage its individual files in File manager instead."_nx;return Bad;}}
        if(std::any_of(nexus::port::KnownPorts.begin(),nexus::port::KnownPorts.end(),[&](const auto& k){return k.shared&&k.nro==port.nro;})){detail="This port uses a shared runtime. Its whole folder cannot be deleted as one game. Use File manager for the game's own data folder."_nx;return Bad;}
        bool covered=false;for(const auto& other:port.roots)if(root!=other&&f::Within(root,other))covered=true;if(covered)continue;
        Info info;R_TRY(disk.Stat(root,info));if(!info.exists)continue;nexusfiles::Selection selection{port.usb,port.generation,{f::Leaf(root),root,info.size,info.directory}};
        nexusfiles::DeletePlan plan;R_TRY(nexusfiles::PrepareDelete(p,selection,plan,detail));removal.bytes+=plan.snapshot.bytes;removal.folders.push_back(std::move(plan));
    }
    if(removal.folders.empty()){detail="No linked port files remain on this storage. You can still remove its HOME shortcut separately."_nx;return Bad;}detail=port.name+"\n\nPermanently delete these folders/files?\n"_nx;
    for(const auto& plan:removal.folders)detail+=std::string(port.usb?"USB:":"SD:")+plan.source.item.path+"\n";
    detail+="\n"+utils::formatSizeStorage(removal.bytes)+"\n\nSaves and settings INSIDE these folders will also be deleted. There is no backup or recycle bin. Console saves are kept. The HOME shortcut is removed separately."_nx;return 0;
}
Result Remove(ui::ProgressBox* p,const Removal& removal,std::string& detail){
    // The confirmation's worker just prepared ownership and metadata. Delete
    // exact entries without hashing game payloads or scanning the tree again.
    R_TRY(GuardPort(removal.port));
    u64 total{},done{};for(const auto& plan:removal.folders)total+=plan.snapshot.entries.size();
    for(const auto& plan:removal.folders){ui::ProgressScope scope(p,nexus::WorkPart(done,total),nexus::WorkPart(done+plan.snapshot.entries.size(),total));
        nexusfiles::Report report;const auto rc=nexusfiles::Delete(p,plan,report);if(R_FAILED(rc)){detail=report.detail+"\nEarlier folders may already have been removed. Refresh before retrying."_nx;return rc;}done+=plan.snapshot.entries.size();}
    detail="Port files removed. Console save data was kept. Remove its HOME shortcut separately if no longer needed."_nx;return 0;
}
Result CreateShortcut(ui::ProgressBox* p,Port& port){
    R_TRY(GuardPort(port));R_UNLESS(core::Root(port.nro)&&nexus_vsd::Nro(port.nro),Bad);Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());Info info;R_TRY(disk.Stat(port.nro,info));R_UNLESS(info.exists&&!info.directory,Bad);
    OwoConfig cfg{};cfg.port_shortcut=true;cfg.nro_path="sdmc:"+port.nro;cfg.name=port.name;cfg.args=port.args;cfg.author="Nexus Manager / Port";
    cfg.address_space=port.address==1?ForwarderAddressSpace::Bit36:ForwarderAddressSpace::Bit39;cfg.nacp.supported_language_flag=0xffff;cfg.nacp.accessible_program_index_flags=1;strcpy(cfg.nacp.display_version,"1.0.0");
    if(port.usb){
        nexus_vsd::Profile profile{};R_UNLESS(nexus_vsd::Parse(port.nro,profile),Bad);cfg.nro_path="sdmc:"+std::string(profile.logical);cfg.usb_path=port.nro;
        nexusapp::Game game;R_TRY(nexusapp::ReadHomebrew({port.name,port.nro,info.size,false},port.generation,game));
        if(R_SUCCEEDED(nexusapp::ReadHomebrewIcon(game,cfg.icon)))cfg.icon=ImageNormalizeIcon(cfg.icon);
        std::array<u8,128> header{};u64 got{};R_TRY(disk.Read(port.nro,0,header.data(),header.size(),got));R_UNLESS(got==header.size(),Bad);nexus::library::Assets assets{};R_UNLESS(nexus::library::Header(header,info.size,assets),Bad);
        ui::ProgressScope executable(p,0,3000);Sha256Context hash;sha256ContextCreate(&hash);std::vector<u8> buffer(256*1024);p->NewTransfer("Checking the port executable"_nx);
        for(u64 off=0;off<assets.code;){const auto n=std::min<u64>(buffer.size(),assets.code-off);R_TRY(disk.Read(port.nro,off,buffer.data(),n,got));R_UNLESS(got==n,Bad);sha256ContextUpdate(&hash,buffer.data(),n);off+=n;p->UpdateTransferBytes(off,assets.code);}cfg.usb_nro_hash.resize(32);sha256ContextGetHash(&hash,cfg.usb_nro_hash.data());
    }else{NroEntry entry{};R_TRY(nro_parse(port.nro.c_str(),entry));cfg.icon=nro_get_icon(port.nro.c_str(),entry.icon_size,entry.icon_offset);if(!cfg.icon.empty())cfg.icon=ImageNormalizeIcon(cfg.icon);}
    if(cfg.icon.empty()){fs::FsStdio rom;R_TRY(rom.read_entire_file("romfs:/usb-loader-icon.jpg",cfg.icon));}
    R_TRY(disk.Check());{ui::ProgressScope install(p,port.usb?3000:0,10000);R_TRY(install_forwarder(p,cfg,NcmStorageId_BuiltInUser));}auto updated=port;
    const auto identity="Nexus Manager port v1:"_nx+(port.usb?"usb:"+cfg.usb_path:"sd:"+cfg.nro_path)+":"+cfg.args;
    u8 hash[32];sha256CalculateHash(hash,identity.data(),identity.size());u64 id{};memcpy(&id,hash,8);updated.application=0x0500000000000000ULL|(id&0x00FFFFFFFFFFF000ULL);R_TRY(SaveProfile(p,updated));port=std::move(updated);return 0;
}
Result ListMods(ui::ProgressBox* p,const Port& port,std::vector<Mod>& mods,std::string& detail){mods.clear();Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());Info i;R_TRY(disk.Stat(ModRoot(port),i));if(!i.exists)return 0;R_UNLESS(i.directory,Bad);std::vector<nexusapp::Item> entries;R_TRY(disk.List(ModRoot(port),entries));R_UNLESS(entries.size()<=128,Bad);
    for(const auto& e:entries){if(!e.directory||!core::Hex(e.name,16))continue;Mod mod;mod.path=e.path;Info manifest;R_TRY(disk.Stat(e.path+"/manifest.json",manifest));if(!manifest.exists)continue;
        core::ModPlan plan;R_TRY(LoadMod(disk,port,mod,"manifest.json",plan));mod.name=plan.name;mod.root=plan.root;mod.bytes=plan.bytes;mod.files=plan.files.size();Info active,session;R_TRY(disk.Stat(e.path+"/active",active));R_TRY(disk.Stat(e.path+"/session.json",session));R_UNLESS(!active.exists||session.exists,Bad);mod.active=active.exists;mod.pending=session.exists&&!active.exists;mods.push_back(std::move(mod));}
    detail="Enable a mod to apply its files. Disable restores the originals. Recover completes restoration after an interrupted change."_nx;return 0;}
Result ReviewEnable(ui::ProgressBox* p,const Port& port,const Mod& mod,core::ModPlan& plan,std::string& detail){R_TRY(GuardPort(port));Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());R_TRY(LoadMod(disk,port,mod,"manifest.json",plan));if(port.usb&&f::Fold(plan.root).starts_with("/atmosphere/")){detail="USB Atmosphere mods need the firmware mapping and SD cache managed by More > Game mods. Use that tool for these folders; this switcher supports NRO port folders on SD/USB and Atmosphere port folders on SD.";return Bad;}Info session;R_TRY(disk.Stat(mod.path+"/session.json",session));R_UNLESS(!session.exists,Bad);
    std::vector<Mod> all;R_TRY(ListMods(p,port,all,detail));for(const auto& other:all)if((other.active||other.pending)&&core::Overlap(other.root,plan.root)){detail="Disable the active mod for this folder first. One mod is active per destination, so originals can be restored reliably."_nx;return Bad;}
    std::vector<nexusapp::Item> groups;R_TRY(disk.List(std::string(core::Store),groups));R_UNLESS(groups.size()<=1024,Bad);
    for(const auto& group:groups){if(!group.directory||!core::Hex(group.name,16))continue;std::vector<nexusapp::Item> packs;R_TRY(disk.List(group.path,packs));R_UNLESS(packs.size()<=128,Bad);
        for(const auto& pack:packs){if(!pack.directory||!core::Hex(pack.name,16)||pack.path==plan.store)continue;Info active;R_TRY(disk.Stat(pack.path+"/session.json",active));if(!active.exists)continue;
            std::string text;R_TRY(disk.Text(pack.path+"/session.json",text));core::ModPlan other;R_UNLESS(Decode(text,other)&&other.store==pack.path,Bad);
            if(core::Overlap(other.root,plan.root)){detail="Another mod manages files in this destination. Disable / recover it before enabling this one."_nx;return Bad;}}}
    std::vector<nexusapp::Item> checked;R_TRY(disk.List(plan.root,checked));
    size_t replaced{},checked_files{};for(auto& file:plan.files){ui::ProgressScope range(p,nexus::WorkPart(checked_files,plan.files.size()),nexus::WorkPart(checked_files+1,plan.files.size()));++checked_files;Info i;const auto path=f::Child(plan.root,file.relative);R_TRY(disk.Stat(path,i));R_UNLESS(!i.directory,Bad);file.had_original=i.exists;file.original_size=i.size;if(i.exists){R_TRY(disk.Hash(path,i.size,file.original_hash));++replaced;}}
    detail=plan.name+"\n\nDestination: "_nx+std::string(port.usb?"USB:":"SD:")+plan.root+"\n"+std::to_string(plan.files.size())+" files / "_nx+utils::formatSizeStorage(plan.bytes)+"\n"+std::to_string(replaced)+" existing files will be kept for restoration.\n\nClose the port first. Disable restores the originals. If a mod changes its NRO, recreate the USB shortcut afterwards."_nx;return 0;}
Result Enable(ui::ProgressBox* p,const Port& port,const core::ModPlan& plan,std::string& detail){R_UNLESS(Allowed(port,plan),Bad);Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());
    Mod mod;mod.path=plan.store;core::ModPlan current;{ui::ProgressScope review(p,0,2000);R_TRY(ReviewEnable(p,port,mod,current,detail));}R_UNLESS(Encode(current)==Encode(plan),Bad);ui::ProgressScope apply(p,2000,10000);ModIO io(disk,plan,detail);if(!core::Enable(io,plan))return io.rc?io.rc:Bad;detail="Mod enabled. Close and reopen the port to use it. Original files are retained until you disable the mod."_nx;return 0;}
Result Disable(ui::ProgressBox* p,const Port& port,const Mod& mod,std::string& detail){R_TRY(GuardPort(port));Disk disk(p,port.usb,port.generation);R_TRY(disk.Open());core::ModPlan plan;R_TRY(LoadMod(disk,port,mod,"session.json",plan));ModIO io(disk,plan,detail);if(!core::Disable(io,plan))return io.rc?io.rc:Bad;detail="Mod disabled. Original files restored; the mod is kept for reuse. Empty folders may remain."_nx;return 0;}
}
namespace sphaira::nexusports {
namespace {
struct ZipReader {Disk* disk;std::string path;u64 size{},offset{};bool error{};};
voidpf ZCALLBACK ZipOpen(voidpf opaque,const void*,int mode){auto* r=static_cast<ZipReader*>(opaque);if((mode&ZLIB_FILEFUNC_MODE_READ)==0)return nullptr;r->offset=0;return r;}
uLong ZCALLBACK ZipRead(voidpf,voidpf stream,void* out,uLong size){auto* r=static_cast<ZipReader*>(stream);u64 got{};if(r->offset>r->size){r->error=true;return 0;}const auto n=std::min<u64>(size,r->size-r->offset);const auto rc=r->disk->Read(r->path,r->offset,out,n,got);if(R_FAILED(rc)||got!=n){r->error=true;return 0;}r->offset+=got;return got;}
uLong ZCALLBACK ZipWrite(voidpf,voidpf,const void*,uLong){return 0;}
ZPOS64_T ZCALLBACK ZipTell(voidpf,voidpf stream){return static_cast<ZipReader*>(stream)->offset;}
long ZCALLBACK ZipSeek(voidpf,voidpf stream,ZPOS64_T off,int origin){auto* r=static_cast<ZipReader*>(stream);const auto delta=static_cast<s64>(off);const __int128 base=origin==ZLIB_FILEFUNC_SEEK_SET?0:origin==ZLIB_FILEFUNC_SEEK_CUR?r->offset:origin==ZLIB_FILEFUNC_SEEK_END?r->size:-1;const __int128 next=base+delta;if(base<0||next<0||next>r->size){r->error=true;return -1;}r->offset=static_cast<u64>(next);return 0;}
int ZCALLBACK ZipClose(voidpf,voidpf){return 0;}
int ZCALLBACK ZipError(voidpf,voidpf stream){return static_cast<ZipReader*>(stream)->error;}
Result CopyPayload(ui::ProgressBox* p,Disk& source,Disk& dest,const std::string& from,const std::string& to,u64 size,std::string& hash){
    Sha256Context sha;sha256ContextCreate(&sha);R_TRY(dest.Create(to,size));
    {ui::ProgressScope copy(p,0,5000);std::vector<u8> buffer(256*1024);p->NewTransfer(from);
        for(u64 off=0;off<size;){const auto n=std::min<u64>(buffer.size(),size-off);u64 got{};R_TRY(source.Read(from,off,buffer.data(),n,got));R_UNLESS(got==n,Bad);sha256ContextUpdate(&sha,buffer.data(),n);R_TRY(dest.Write(to,off,buffer.data(),n));off+=n;p->UpdateTransferBytes(off,size);}
        R_TRY(dest.Flush());}
    u8 digest[32];sha256ContextGetHash(&sha,digest);hash=nexusapp::HexId(digest);
    {ui::ProgressScope check(p,5000,7500);std::string original;R_TRY(source.Hash(from,size,original));R_UNLESS(original==hash,Bad);}
    {ui::ProgressScope check(p,7500,10000);std::string readback;R_TRY(dest.Hash(to,size,readback));R_UNLESS(readback==hash,Bad);}
    return 0;
}

}
Result ImportMod(ui::ProgressBox* p,const Port& port,const std::string& root,const nexusfiles::Selection& selected,Mod& mod,std::string& detail){
    detail="Choose a mod folder or ZIP containing files relative to its destination. Other game or system folders cannot be imported here."_nx;
    R_UNLESS(core::Root(root)&&std::find(port.roots.begin(),port.roots.end(),root)!=port.roots.end(),Bad);
    if(port.usb&&f::Fold(root).starts_with("/atmosphere/")){detail="For Atmosphere content stored on USB, use More > Game mods so its firmware mapping and SD cache are updated together. Port mod import supports NRO folders on SD/USB and Atmosphere folders on SD.";return Bad;}
    Disk target(p,port.usb,port.generation),source(p,selected.usb,selected.generation);R_TRY(target.Open());R_TRY(source.Open());Info targetinfo,srcinfo;R_TRY(target.Stat(root,targetinfo));R_UNLESS(targetinfo.exists&&targetinfo.directory,Bad);R_TRY(source.Stat(selected.item.path,srcinfo));R_UNLESS(srcinfo.exists&&srcinfo.directory==selected.item.directory&&srcinfo.size==selected.item.size,Bad);
    core::ModPlan plan;plan.root=root;plan.name=selected.item.name;plan.store=ModRoot(port)+"/"+Random();
    detail="Import stopped. Original port files are unchanged. An incomplete copy may remain at "_nx+std::string(port.usb?"USB:":"SD:")+plan.store+"; it can be reviewed in File manager."_nx;
    const auto add=[&](const std::string& relative,u64 size)->Result{R_UNLESS(core::Relative(relative)&&plan.files.size()<core::MaxModFiles&&size<=core::MaxModBytes-plan.bytes&&core::Root(f::Child(root,relative))&&f::Canonical(plan.store+"/original/"+relative),Bad);
        for(const auto& existing:plan.files)R_UNLESS(!core::Overlap(existing.relative,relative),Bad);plan.files.push_back({relative,{},size,false,{},0});plan.bytes+=size;return 0;};
    if(srcinfo.directory){
        R_UNLESS(!f::Within(selected.item.path,std::string(core::Store)),Bad);std::vector<std::pair<std::string,unsigned>> todo{{selected.item.path,0}};
        std::vector<std::string> sources;size_t seen{};
        for(size_t at=0;at<todo.size();++at){auto [path,depth]=todo[at];R_UNLESS(depth<32,Bad);std::vector<nexusapp::Item> entries;R_TRY(source.List(path,entries));
            for(const auto& item:entries){R_UNLESS(++seen<=16384&&f::Safe(item.path),Bad);if(item.directory){todo.emplace_back(item.path,depth+1);continue;}
                const auto rel=core::ArchivePath(item.path.substr(selected.item.path.size()+1),root);R_TRY(add(rel,item.size));sources.push_back(item.path);}}
        R_UNLESS(!plan.files.empty(),Bad);R_TRY(target.Space(plan.bytes));
        u64 done{},total{};for(const auto& file:plan.files)total+=std::max<u64>(file.size,1);
        for(size_t i=0;i<plan.files.size();++i){auto& file=plan.files[i];const auto weight=std::max<u64>(file.size,1);
            ui::ProgressScope range(p,nexus::WorkPart(done,total,9500),nexus::WorkPart(done+weight,total,9500));
            R_TRY(CopyPayload(p,source,target,sources[i],plan.store+"/payload/"+file.relative,file.size,file.hash));done+=weight;}
    }else{
        R_UNLESS(f::Fold(selected.item.path).ends_with(".zip"),Bad);
        ZipReader reader{&source,selected.item.path,srcinfo.size};zlib_filefunc64_def funcs{};funcs.zopen64_file=ZipOpen;funcs.zread_file=ZipRead;funcs.zwrite_file=ZipWrite;funcs.ztell64_file=ZipTell;funcs.zseek64_file=ZipSeek;funcs.zclose_file=ZipClose;funcs.zerror_file=ZipError;funcs.opaque=&reader;
        auto zip=unzOpen2_64(selected.item.path.c_str(),&funcs);R_UNLESS(zip,Bad);struct Close{unzFile f;~Close(){unzClose(f);}}close{zip};
        unz_global_info64 global{};R_UNLESS(unzGetGlobalInfo64(zip,&global)==UNZ_OK&&global.number_entry&&global.number_entry<=16384,Bad);
        struct Archived {unz64_file_pos position;};std::vector<Archived> entries;
        R_UNLESS(unzGoToFirstFile(zip)==UNZ_OK,Bad);
        // Validate every path/type/size before extracting even the first file.
        for(u64 i=0;i<global.number_entry;++i){R_TRY(source.Check());unz_file_info64 info{};char name[256]{};R_UNLESS(unzGetCurrentFileInfo64(zip,&info,name,sizeof(name),nullptr,0,nullptr,0)==UNZ_OK&&info.size_filename>0&&info.size_filename<sizeof(name)&&strnlen(name,sizeof(name))==info.size_filename,Bad);
            const unsigned type=(info.external_fa>>16)&0170000;R_UNLESS((type==0||type==0100000||type==0040000)&&!(info.flag&1)&&(info.compression_method==0||info.compression_method==8),Bad);
            std::string relative=name;const bool directory=relative.ends_with('/');if(directory)relative.pop_back();R_UNLESS(core::Relative(relative),Bad);
            if(!directory){R_TRY(add(core::ArchivePath(relative,root),info.uncompressed_size));Archived entry{};R_UNLESS(unzGetFilePos64(zip,&entry.position)==UNZ_OK,Bad);entries.push_back(entry);}
            if(i+1<global.number_entry)R_UNLESS(unzGoToNextFile(zip)==UNZ_OK,Bad);
        }
        R_UNLESS(!plan.files.empty()&&!reader.error,Bad);R_TRY(target.Space(plan.bytes));std::vector<u8> buffer(256*1024);
        u64 done{},total{};for(const auto& file:plan.files)total+=std::max<u64>(file.size,1);
        for(size_t i=0;i<entries.size();++i){auto& file=plan.files[i];const auto weight=std::max<u64>(file.size,1);ui::ProgressScope range(p,nexus::WorkPart(done,total,9500),nexus::WorkPart(done+weight,total,9500));done+=weight;R_UNLESS(unzGoToFilePos64(zip,&entries[i].position)==UNZ_OK&&unzOpenCurrentFile(zip)==UNZ_OK,Bad);bool open=true;struct Current{unzFile f;bool& open;~Current(){if(open)unzCloseCurrentFile(f);}}current{zip,open};
            const auto destination=plan.store+"/payload/"+file.relative;R_TRY(target.Create(destination,file.size));Sha256Context sha;sha256ContextCreate(&sha);{ui::ProgressScope extract(p,0,7000);p->NewTransfer(file.relative);
            for(u64 off=0;off<file.size;){R_TRY(source.Check());const auto n=std::min<u64>(buffer.size(),file.size-off);const auto got=unzReadCurrentFile(zip,buffer.data(),n);R_UNLESS(got>0&&static_cast<u64>(got)<=n,Bad);sha256ContextUpdate(&sha,buffer.data(),got);R_TRY(target.Write(destination,off,buffer.data(),got));off+=got;p->UpdateTransferBytes(off,file.size);}
            R_UNLESS(unzReadCurrentFile(zip,buffer.data(),1)==0,Bad);const auto crc=unzCloseCurrentFile(zip);open=false;R_UNLESS(crc==UNZ_OK&&!reader.error,Bad);R_TRY(target.Flush());}u8 digest[32];sha256ContextGetHash(&sha,digest);file.hash=nexusapp::HexId(digest);{ui::ProgressScope verify(p,7000,10000);std::string check;R_TRY(target.Hash(destination,file.size,check));R_UNLESS(check==file.hash,Bad);}
        }
    }
    ui::ProgressScope finish(p,9500,10000);R_UNLESS(core::Valid(plan),Bad);R_TRY(target.NewText(plan.store+"/manifest.json",Encode(plan)));mod={plan.name,plan.store,plan.root,false,false,plan.bytes,plan.files.size()};detail="Mod imported and checked. It is disabled. Choose Enable to review changes before applying it."_nx;return 0;
}
}
