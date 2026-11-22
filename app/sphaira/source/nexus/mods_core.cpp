#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/mods_core.hpp"
#include "nexus/usb_mod_index.hpp"
#include <yyjson.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <climits>
namespace nexus::mods {
namespace {
constexpr size_t MiB=1024*1024,Limit=64*MiB;
const std::string Cache="switch/usbloader/mod-cache/",Pending=Cache+"pending.json",Backups="switch/usbloader/mod-backups/";
bool Path(const std::string& p,bool absolute=false){return nexus_mod::Path(p.data(),p.size(),absolute);}
std::string Join(const std::string& a,const std::string& b){return a=="/"?a+b:a+"/"+b;}
bool Name(const std::string& s){return Path(s)&&s.find('/')==s.npos;}
std::string Lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
bool Hex(const std::string& s,size_t lo,size_t hi){return s.size()>=lo&&s.size()<=hi&&s.find_first_not_of("0123456789abcdefABCDEF")==s.npos;}
std::string HexBytes(const void* p,size_t n){std::string s;for(size_t i=0;i<n;++i){char a[3];snprintf(a,3,"%02x",((const uint8_t*)p)[i]);s+=a;}return s;}
uint64_t Align(uint64_t n,uint64_t a){return (n+a-1)&~(a-1);}
void W32(Bytes& b,size_t at,uint32_t v){std::memcpy(b.data()+at,&v,4);}
void W64(Bytes& b,size_t at,uint64_t v){std::memcpy(b.data()+at,&v,8);}
uint32_t HashName(uint32_t p,const std::string& name){p^=123456789;for(unsigned char c:name){p=(p>>5)|(p<<27);p^=c;}return p;}
std::string Str(yyjson_val* v,const char* key){auto p=yyjson_obj_get(v,key);return yyjson_is_str(p)?std::string(yyjson_get_str(p),yyjson_get_len(p)):"";}
std::string Quote(const std::string& s){std::string o="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=c;}else if(c<32){char tmp[7];snprintf(tmp,7,"\\u%04x",c);o+=tmp;}else o+=c;}return o+'"';}
Bytes Data(const std::string& s){return Bytes(s.begin(),s.end());}
bool ReadHash(IO& io,const std::string& path,std::string& sha){Info i;if(!io.Stat(false,path,i))return false;sha.clear();if(!i.exists)return true;if(i.directory)return io.Fail("SD destination is a directory: "_nx+path);Bytes b;if(!io.Read(false,path,Limit,b))return false;sha=io.Hash(b);return true;}
bool Ips(const Bytes& d){
    if(d.size()<8)return false;
    size_t w;std::string end;
    if(!memcmp(d.data(),"PATCH",5)){w=3;end="EOF";}else if(!memcmp(d.data(),"IPS32",5)){w=4;end="EEOF";}else return false;
    for(size_t at=5;at<d.size();){
        if(d.size()-at>=end.size()&&!memcmp(d.data()+at,end.data(),end.size()))return at+end.size()==d.size();
        if(d.size()-at<w+2)return false;at+=w;size_t n=(d[at]<<8)|d[at+1];at+=2;
        if(!n){if(d.size()-at<3||!(d[at]|d[at+1]))return false;at+=3;}else {if(n>d.size()-at)return false;at+=n;}
    }return false;
}
bool ExeName(std::string name){if(name.ends_with(".stub"))name.resize(name.size()-5);return name=="main.npdm"||name=="main"||name=="rtld"||name=="sdk"||(name.size()==7&&name.starts_with("subsdk")&&std::string("0123456789abcdef").find(name.back())!=std::string::npos);}
bool Allowed(const std::string& key,const std::string& p){
    if(!Title(key)&&!(key.size()==20&&key.starts_with("ips-")&&Hex(key.substr(4),16,16)))return false;
    if(!Path(p))return false;
    if(p==Cache+key+".json")return true;
    if(Title(key)){
        const auto base="atmosphere/contents/"+key+"/";
        if(p==base+"usb_romfs.idx"||p==base+"usb_exefs.enabled")return true;
        if(p.starts_with(base+"exefs/")&&ExeName(p.substr(base.size()+6)))return true;
        // Existing PC M1 journals may contain reviewed per-title IPS groups.
        const auto prefix="atmosphere/exefs_patches/nexus_m1_"+key+"_";
        if(p.starts_with(prefix)){const auto tail=p.substr(prefix.size());auto slash=tail.find('/');return slash!=tail.npos&&Name(tail.substr(0,slash))&&Name(tail.substr(slash+1))&&tail.ends_with(".ips");}
    }else if(key.size()==20&&key.starts_with("ips-")&&Hex(key.substr(4),16,16)){
        const auto base="atmosphere/exefs_patches/nexus_usb_"+key+"/";
        if(p.starts_with(base)){const auto n=p.substr(base.size());return n.ends_with(".ips")&&Hex(n.substr(0,n.size()-4),16,64)&&!(n.size()%2);}
    }
    return false;
}
bool ReadJournal(IO& io,const std::string& key,std::vector<Cached>& files,std::string& root){
    Info info;if(!io.Stat(false,Cache+key+".json",info))return false;if(!info.exists)return true;
    Bytes b;if(!io.Read(false,Cache+key+".json",MiB,b))return false;
    auto doc=yyjson_read((const char*)b.data(),b.size(),0);if(!doc)return io.Fail("Invalid ownership journal; no files changed."_nx);
    auto r=yyjson_doc_get_root(doc),a=yyjson_obj_get(r,"files");const auto fmt=Str(r,"format");
    bool ok=(fmt=="nexus-usb-mods-m1"||fmt=="nexus-usb-mods-app1")&&Str(r,"title")==key&&yyjson_is_arr(a)&&yyjson_arr_size(a)<=512;
    root=Str(r,"usb_romfs");std::set<std::string> seen;
    size_t i,n;yyjson_val* v;
    yyjson_arr_foreach(a,i,n,v){
        Cached f{Str(v,"path"),Str(v,"sha256"),yyjson_get_uint(yyjson_obj_get(v,"size")),yyjson_get_bool(yyjson_obj_get(v,"owned"))};
        ok&=Allowed(key,f.path)&&f.path!=Cache+key+".json"&&Hex(f.sha,64,64)&&seen.insert(Lower(f.path)).second&&f.size<=Limit;
        files.push_back(std::move(f));
    }
    yyjson_doc_free(doc);return ok||io.Fail("Ownership journal failed validation; no files changed."_nx);
}
bool NewVerified(IO& io,bool usb,const std::string& path,const Bytes& b){
    if(!io.New(usb,path,b))return false;Bytes read;
    if(!io.Read(usb,path,b.size(),read))return false;
    return read==b||io.Fail("Write verification failed: "_nx+path);
}
struct Node {std::string name,path;size_t parent{};uint32_t off{};uint64_t size{},data{};std::vector<size_t> dirs,files;};
bool Walk(IO& io,const std::string& root,const std::string& rel,size_t parent,unsigned depth,std::vector<Node>& dirs,std::vector<Node>& files){
    if(depth>24||dirs.size()>=2048)return io.Fail("Mod exceeds 24 directory levels / 2048 directories."_nx);
    const auto id=dirs.size();dirs.push_back({rel.empty()?"":rel.substr(rel.find_last_of('/')+1),rel,parent});
    std::vector<Entry> es;if(!io.Check()||!io.List(rel.empty()?root:Join(root,rel),es))return false;
    std::sort(es.begin(),es.end(),[](auto& a,auto& b){return a.name<b.name;});std::set<std::string> names;
    for(const auto& e:es){
        if(rel.empty()&&e.name==".nexus-mod-id")continue;
        const auto p=rel.empty()?e.name:Join(rel,e.name);
        if(!Name(e.name)||!Path(p)||root.size()+1+p.size()>=768||!names.insert(Lower(e.name)).second)return io.Fail("Invalid, duplicate or overlong USB mod path: "_nx+p);
        if(!e.directory){if(files.size()>=nexus_mod::MaxFiles||e.size>INT64_MAX)return io.Fail("Mod exceeds the 1536-file/size limit."_nx);dirs[id].files.push_back(files.size());files.push_back({e.name,p,id,0,e.size});}
    }
    for(const auto& e:es)if(e.directory){dirs[id].dirs.push_back(dirs.size());if(!Walk(io,root,rel.empty()?e.name:Join(rel,e.name),id,depth+1,dirs,files))return false;}
    return true;
}
}
bool Title(const std::string& s,uint64_t* out){if(!Hex(s,16,16))return false;uint64_t n=0;for(char c:Lower(s))n=(n<<4)|(c<='9'?c-'0':c-'a'+10);if(out)*out=n;return nexus_mod::GameId(n);}
bool BuildIndex(IO& io,const std::string& root,const std::string& title,const Bytes& token,Bytes& result,uint64_t& total,size_t& count){
    uint64_t tid;if(!Title(title,&tid)||!Path(root,true)||token.size()!=16)return io.Fail("Invalid title/root/marker."_nx);
    std::vector<Node> dirs,files;if(!Walk(io,root,"",0,0,dirs,files))return false;if(files.empty())return io.Fail("RomFS has no files."_nx);
    std::vector<size_t> order;for(size_t i=0;i<files.size();++i)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return files[a].path<files[b].path;});
    uint32_t ds=0,fs=0;for(auto& d:dirs){d.off=ds;ds+=24+Align(d.name.size(),4);}for(auto i:order){files[i].off=fs;fs+=32+Align(files[i].name.size(),4);}
    const uint64_t dh=80,dt=92,fh=dt+ds,ft=fh+12,ms=ft+fs,dat=Align(ms,512);
    if(640+544*files.size()+ms>MiB)return io.Fail("Mod index exceeds 1 MiB."_nx);
    Bytes m(ms);uint64_t hdr[]={80,dh,12,dt,ds,fh,12,ft,fs,dat};memcpy(m.data(),hdr,80);
    uint32_t db[3]={UINT32_MAX,UINT32_MAX,UINT32_MAX},fb[3]={UINT32_MAX,UINT32_MAX,UINT32_MAX};
    result.assign(640+544*files.size()+ms,0);uint64_t end=dat;total=0;count=files.size();size_t ri=0;
    for(auto i:order){auto& f=files[i];end=Align(end,16);if(f.size>uint64_t(INT64_MAX)-end)return io.Fail("RomFS size overflow."_nx);f.data=end-dat;auto at=640+544*ri++;W64(result,at,end);W64(result,at+8,f.size);W32(result,at+16,f.path.size());memcpy(result.data()+at+24,f.path.data(),f.path.size());end+=f.size;total+=f.size;}
    for(size_t i=0;i<dirs.size();++i){auto& d=dirs[i];const auto par=dirs[d.parent].off;uint32_t sibling=UINT32_MAX;
        if(i){const auto& sibs=dirs[d.parent].dirs;auto j=std::find(sibs.begin(),sibs.end(),i);if(++j!=sibs.end())sibling=dirs[*j].off;}
        const auto bucket=HashName(par,d.name)%3;uint32_t h[]={par,sibling,d.dirs.empty()?UINT32_MAX:dirs[d.dirs[0]].off,d.files.empty()?UINT32_MAX:files[d.files[0]].off,db[bucket],uint32_t(d.name.size())};
        memcpy(m.data()+dt+d.off,h,24);memcpy(m.data()+dt+d.off+24,d.name.data(),d.name.size());db[bucket]=d.off;
        for(size_t j=0;j<d.files.size();++j){const auto& f=files[d.files[j]];const auto b=HashName(d.off,f.name)%3,at=uint32_t(ft+f.off);
            W32(m,at,d.off);W32(m,at+4,j+1<d.files.size()?files[d.files[j+1]].off:UINT32_MAX);W64(m,at+8,f.data);W64(m,at+16,f.size);W32(m,at+24,fb[b]);W32(m,at+28,f.name.size());memcpy(m.data()+at+32,f.name.data(),f.name.size());fb[b]=f.off;
        }
    }
    memcpy(m.data()+dh,db,12);memcpy(m.data()+fh,fb,12);memcpy(result.data(),"NXUMOD1",7);W32(result,8,1);W32(result,12,640);W64(result,16,tid);W64(result,24,end);W32(result,32,ms);W32(result,36,count);W32(result,40,root.size());memcpy(result.data()+48,token.data(),16);memcpy(result.data()+128,root.data(),root.size());memcpy(result.data()+640+544*count,m.data(),m.size());
    nexus_mod::View view;return nexus_mod::Parse(&view,result.data(),result.size(),tid)||io.Fail("Generated index failed the firmware validator."_nx);
}
bool HasPending(IO& io,bool& pending){Info s;if(!io.Stat(false,Pending,s))return false;pending=s.exists;return true;}
bool Scan(IO& io,const std::string& atmosphere,std::vector<Candidate>& out){
    out.clear();if(!Path(atmosphere,true))return io.Fail("Choose an Atmosphere directory on USB."_nx);
    Info info;if(!io.Stat(true,Join(atmosphere,"contents"),info))return false;
    if(info.exists&&info.directory){std::vector<Entry> es;if(!io.List(Join(atmosphere,"contents"),es))return false;
        for(auto& e:es)if(e.directory&&Title(e.name)){
            Candidate c{Lower(e.name),Join(Join(atmosphere,"contents"),e.name),Lower(e.name)};
            if(!io.Stat(true,Join(c.path,"romfs"),info))return false;c.romfs=info.exists&&info.directory;
            if(!io.Stat(true,Join(c.path,"exefs"),info))return false;c.exefs=info.exists&&info.directory;
            if(!io.Stat(false,Cache+c.id+".json",info))return false;c.registered=info.exists;
            if(c.romfs||c.exefs)out.push_back(c);if(out.size()>512)return io.Fail("Too many mod groups; choose a smaller folder."_nx);
        }
    }
    if(!io.Stat(true,Join(atmosphere,"exefs_patches"),info))return false;
    if(info.exists&&info.directory){std::vector<Entry> es;if(!io.List(Join(atmosphere,"exefs_patches"),es))return false;
        for(auto& e:es)if(e.directory&&Name(e.name)){
            Candidate c{e.name,Join(Join(atmosphere,"exefs_patches"),e.name),"",false,false,true};
            c.id="ips-"+io.Hash(Data(c.path)).substr(0,16);if(!io.Stat(false,Cache+c.id+".json",info))return false;c.registered=info.exists;out.push_back(c);if(out.size()>512)return io.Fail("Too many mod groups."_nx);
        }
    }
    std::sort(out.begin(),out.end(),[](auto& a,auto& b){return a.ips!=b.ips?!a.ips:a.name<b.name;});return io.Check();
}
bool Prepare(IO& io,const Candidate& c,bool exefs,Plan& p){
    p={};bool pending;if(!io.Check()||!HasPending(io,pending))return false;if(pending)return io.Fail("An interrupted SD update needs recovery first. Use USB Mods > Recover."_nx);
    if(!Path(c.path,true)||(!c.ips&&!Title(c.id))||!Allowed(c.id,Cache+c.id+".json"))return io.Fail("Invalid mod selection."_nx);
    p.key=c.id;p.title=c.id;std::vector<Cached> old;std::string oldroot;
    if(!ReadJournal(io,c.id,old,oldroot))return false;
    std::map<std::string,Cached> owned;for(auto& f:old)owned[f.path]=f;
    const auto base="atmosphere/contents/"+c.id+"/";std::map<std::string,Bytes> blobs;
    if(!c.ips){for(auto conflict:{"romfs","romfs.bin","exefs.nsp"}){Info s;if(!io.Stat(false,base+conflict,s))return false;if(s.exists)return io.Fail("Existing SD mod conflicts: "_nx+base+conflict+". Move it aside manually first."_nx);}}
    if(c.romfs&&!c.ips){
        p.root=Join(c.path,"romfs");p.marker_path=Join(p.root,".nexus-mod-id");Info s;if(!io.Stat(true,p.marker_path,s))return false;
        if(s.exists){if(s.directory||s.size!=16||!io.Read(true,p.marker_path,16,p.marker))return io.Fail("Invalid USB mod identity marker; leave existing registration unchanged."_nx);}
        else{p.new_marker=true;p.marker.resize(16);io.Random(p.marker.data(),16);}
        if(!BuildIndex(io,p.root,c.id,p.marker,blobs[base+"usb_romfs.idx"],p.data_bytes,p.file_count))return false;
    }
    if(c.ips||(exefs&&c.exefs)){
        std::vector<Entry> es;const auto folder=c.ips?c.path:Join(c.path,"exefs");if(!io.List(folder,es)||es.empty())return io.Fail("Selected patch folder is empty or unavailable."_nx);
        size_t bytes=0;for(const auto& e:es){
            if(e.directory||!Name(e.name))return io.Fail("Patch folders must contain files only."_nx);
            if(c.ips){if(!e.name.ends_with(".ips")||!Hex(e.name.substr(0,e.name.size()-4),16,64)||e.name.size()%2||e.size>4*MiB)return io.Fail("IPS filename must be an unchanged, even-length build ID (16-64 hex digits)."_nx);}
            else if(!ExeName(e.name)||e.size>32*MiB)return io.Fail("Unsupported ExeFS file: "_nx+e.name);
            if(es.size()>510||e.size>Limit-bytes)return io.Fail("Executable cache exceeds 64 MiB / 510 files."_nx);bytes+=e.size;
            Bytes b;if(!io.Read(true,Join(folder,e.name),c.ips?4*MiB:32*MiB,b))return false;
            bool valid=c.ips?Ips(b):e.name.ends_with(".stub")?b.empty():e.name=="main.npdm"?(b.size()>=0x80&&!memcmp(b.data(),"META",4)):(b.size()>=4&&!memcmp(b.data(),"NSO0",4));
            if(!valid)return io.Fail("Malformed executable patch: "_nx+e.name);
            blobs[c.ips?"atmosphere/exefs_patches/nexus_usb_"+c.id+"/"+e.name:base+"exefs/"+e.name]=std::move(b);
        }
        if(!c.ips)blobs[base+"usb_exefs.enabled"]=Data("NXUEXE1\n");
    }
    if(blobs.empty())return io.Fail("No supported files selected."_nx);
    for(auto& [path,b]:blobs){std::string oldsha;if(!ReadHash(io,path,oldsha))return false;const auto sha=io.Hash(b);const auto prev=owned.find(path);
        if(!oldsha.empty()&&oldsha!=sha&&(prev==owned.end()||!prev->second.owned||oldsha!=prev->second.sha))return io.Fail("Unmanaged or locally changed SD file: "_nx+path+". It was not overwritten."_nx);
        const bool ours=oldsha.empty()||(prev!=owned.end()&&prev->second.owned&&oldsha==prev->second.sha);
        owned[path]={path,sha,b.size(),ours};if(oldsha!=sha)p.changes.push_back({path,oldsha,sha,std::move(b)});
    }
    // Previously managed files not in this selection are retained, never pruned.
    if(owned.size()>512)return io.Fail("Ownership journal exceeds 512 files."_nx);
    if(p.root.empty())p.root=oldroot;
    std::string json="{\"format\":\"nexus-usb-mods-app1\",\"title\":"+Quote(c.id)+",\"usb_romfs\":"+Quote(p.root)+",\"files\":[";bool first=true;
    for(auto& [path,f]:owned){if(!first)json+=',';first=false;json+="{\"path\":"+Quote(path)+",\"sha256\":"+Quote(f.sha)+",\"size\":"+std::to_string(f.size)+",\"owned\":"+(f.owned?"true":"false")+"}";}json+="]}";
    Bytes jb=Data(json);std::string oldsha;if(!ReadHash(io,Cache+c.id+".json",oldsha))return false;
    if(oldsha!=io.Hash(jb))p.changes.push_back({Cache+c.id+".json",oldsha,io.Hash(jb),std::move(jb)});
    // Activation index after small cache files; ownership journal last.
    std::stable_sort(p.changes.begin(),p.changes.end(),[&](auto& a,auto& b){auto rank=[&](auto& x){return x.path==Cache+c.id+".json"?2:x.path.ends_with("/usb_romfs.idx")?1:0;};return rank(a)<rank(b);});
    std::array<uint8_t,16> nonce;io.Random(nonce.data(),nonce.size());p.backup_dir=Backups+HexBytes(nonce.data(),nonce.size());return io.Check();
}
bool Apply(IO& io,Plan& p){
    bool pending;if(!io.Check()||!HasPending(io,pending))return false;if(pending)return io.Fail("Recover the previous mod update first."_nx);
    if(p.changes.empty())return true;
    for(const auto& c:p.changes){std::string now;if(!Allowed(p.key,c.path)||!ReadHash(io,c.path,now)||now!=c.old_hash)return io.Fail("SD changed since review. Scan again; no active files changed."_nx);}
    if(!p.marker_path.empty()){
        Info s;if(!io.Stat(true,p.marker_path,s))return false;
        if(!s.exists){if(!p.new_marker||!NewVerified(io,true,p.marker_path,p.marker))return false;}
        else {Bytes now;if(!io.Read(true,p.marker_path,16,now)||now!=p.marker)return io.Fail("USB identity changed since review."_nx);}
    }
    size_t i=0;std::string manifest="{\"format\":1,\"key\":"+Quote(p.key)+",\"backup\":"+Quote(p.backup_dir)+",\"changes\":[";
    for(const auto& c:p.changes){if(!io.Check()||!NewVerified(io,false,p.backup_dir+"/"+std::to_string(i)+".new",c.bytes))return false;
        if(i++)manifest+=',';manifest+="{\"path\":"+Quote(c.path)+",\"old\":"+Quote(c.old_hash)+",\"new\":"+Quote(c.new_hash)+"}";
    }
    manifest+="]}";
    // Recovery record is published before touching any live destination.
    if(!NewVerified(io,false,p.backup_dir+"/transaction.json",Data(manifest))||!io.Rename(p.backup_dir+"/transaction.json",Pending))return false;
    for(size_t j=0;j<p.changes.size();++j){const auto& c=p.changes[j];std::string now;
        if(!io.Check()||!ReadHash(io,c.path,now)||now!=c.old_hash)return io.Fail("Update stopped; use Recover before another registration."_nx);
        const auto slot=p.backup_dir+"/"+std::to_string(j);
        if(!now.empty()&&!io.Rename(c.path,slot+".old"))return false;
        if(!io.Rename(slot+".new",c.path)||!ReadHash(io,c.path,now)||now!=c.new_hash)return io.Fail("Update interrupted; SD backup retained. Use Recover."_nx);
    }
    // Removing the pending name commits, while retaining the whole backup manifest.
    return io.Rename(Pending,p.backup_dir+"/completed.json");
}
bool Recover(IO& io){
    Info inf;if(!io.Stat(false,Pending,inf))return false;if(!inf.exists)return true;
    Bytes b;if(!io.Read(false,Pending,MiB,b))return false;auto d=yyjson_read((const char*)b.data(),b.size(),0);if(!d)return io.Fail("Invalid recovery record; backups preserved for manual recovery."_nx);
    auto r=yyjson_doc_get_root(d);const auto key=Str(r,"key"),backup=Str(r,"backup");auto a=yyjson_obj_get(r,"changes");
    bool ok=yyjson_get_uint(yyjson_obj_get(r,"format"))==1&&backup.starts_with(Backups)&&Hex(backup.substr(Backups.size()),32,32)&&yyjson_is_arr(a)&&yyjson_arr_size(a)<=513;
    std::vector<Change> cs;std::set<std::string> seen;size_t i,n;yyjson_val* v;
    yyjson_arr_foreach(a,i,n,v){Change c{Str(v,"path"),Str(v,"old"),Str(v,"new"),{}};ok&=Allowed(key,c.path)&&(c.old_hash.empty()||Hex(c.old_hash,64,64))&&Hex(c.new_hash,64,64)&&seen.insert(Lower(c.path)).second;cs.push_back(c);}
    yyjson_doc_free(d);if(!ok)return io.Fail("Recovery validation failed; no files changed."_nx);
    // Validate the entire rollback before removing or renaming any current file.
    for(size_t j=0;j<cs.size();++j){std::string now,old;if(!ReadHash(io,cs[j].path,now)||!ReadHash(io,backup+"/"+std::to_string(j)+".old",old))return false;
        if(now==cs[j].old_hash)continue;
        if((!now.empty()&&now!=cs[j].new_hash)||old!=cs[j].old_hash)return io.Fail("Recovery conflict: "_nx+cs[j].path+". Files/backups preserved."_nx);
    }
    for(size_t k=cs.size();k>0;--k){auto& c=cs[k-1];std::string now,old;if(!ReadHash(io,c.path,now))return false;if(now==c.old_hash)continue;
        if(!ReadHash(io,backup+"/"+std::to_string(k-1)+".old",old)||old!=c.old_hash)return io.Fail("Backup changed during recovery; remaining files preserved."_nx);
        if(!now.empty()&&(now!=c.new_hash||!io.Delete(c.path)))return false;
        if(!c.old_hash.empty()&&!io.Rename(backup+"/"+std::to_string(k-1)+".old",c.path))return false;
    }
    return io.Rename(Pending,backup+"/recovered.json");
}
}
