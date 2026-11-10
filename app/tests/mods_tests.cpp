// SPDX-License-Identifier: GPL-3.0-or-later
// Host fault-injection tests exercise the same core linked into the NRO.
#include "nexus/mods_core.hpp"
#include "nexus/usb_mod_index.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <yyjson.h>
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
using namespace nexus::mods;
static const std::string Id="0100b00b51230000",Root="/atmosphere/contents/"+Id+"/romfs",Index="atmosphere/contents/"+Id+"/usb_romfs.idx",Journal="switch/usbloader/mod-cache/"+Id+".json",Pending="switch/usbloader/mod-cache/pending.json";
struct Fake:IO {
    std::map<std::string,Bytes> usb,sd;
    std::map<std::string,uint64_t> sizes;
    std::set<std::string> dirs;
    int mutations{},fail_at=-1;bool failed{},media=true;unsigned random_counter{};
    bool Fault(){if(++mutations==fail_at){failed=true;return Fail("Injected IO failure");}return true;}
    bool Check() override {return (!failed&&media)||Fail("Interrupted/media changed");}
    bool Stat(bool u,const std::string& p,Info& i) override {
        if(!Check())return false;i={};auto& fs=u?usb:sd;auto it=fs.find(p);if(it!=fs.end()){i={true,false,it->second.size()};return true;}
        if(u&&sizes.contains(p)){i={true,false,sizes.at(p)};return true;}
        if(u&&dirs.contains(p)){i={true,true,0};return true;}
        const auto prefix=p+"/";for(auto& [k,v]:fs)if(k.starts_with(prefix)){i={true,true,0};break;}return true;
    }
    bool List(const std::string& p,std::vector<Entry>& es) override {
        if(!Check())return false;std::map<std::string,Entry> found;const auto pre=p+"/";
        auto add=[&](const std::string& k,uint64_t n,bool dir){if(!k.starts_with(pre))return;auto rel=k.substr(pre.size());auto at=rel.find('/');auto name=rel.substr(0,at);if(at!=rel.npos)dir=true;found[name]={name,n,dir};};
        for(auto& [k,v]:usb)add(k,v.size(),false);for(auto& [k,v]:sizes)add(k,v,false);for(auto& k:dirs)add(k,0,true);
        es.clear();for(auto& [k,e]:found)es.push_back(e);return true;
    }
    bool Read(bool u,const std::string& p,size_t limit,Bytes& b) override {if(!Check())return false;auto& fs=u?usb:sd;auto it=fs.find(p);if(it==fs.end()||it->second.size()>limit)return Fail("Missing/large");b=it->second;return true;}
    bool New(bool u,const std::string& p,const Bytes& b) override {if(!Check()||!Fault())return false;auto& fs=u?usb:sd;if(fs.contains(p))return Fail("Exists");fs[p]=b;return true;}
    bool Rename(const std::string& a,const std::string& b) override {if(!Check()||!Fault())return false;if(!sd.contains(a)||sd.contains(b))return Fail("Rename conflict");sd[b]=sd.at(a);sd.erase(a);return true;}
    bool Delete(const std::string& p) override {if(!Check()||!Fault())return false;if(!sd.erase(p))return Fail("Delete missing");return true;}
    std::string Hash(const Bytes& b) override {
        BCRYPT_ALG_HANDLE alg;BCRYPT_HASH_HANDLE h;assert(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
        assert(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)>=0);assert(BCryptHashData(h,(PUCHAR)b.data(),b.size(),0)>=0);unsigned char digest[32];assert(BCryptFinishHash(h,digest,32,0)>=0);BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);
        std::string s;for(auto c:digest){char tmp[3];snprintf(tmp,3,"%02x",c);s+=tmp;}return s;
    }
    void Random(void* out,size_t n) override {memset(out,++random_counter,n);}
    void Resume(){failed=false;media=true;fail_at=-1;error.clear();}
};
Bytes B(const std::string& s){return Bytes(s.begin(),s.end());}
Candidate Game(){return {Id,"/atmosphere/contents/"+Id,Id,true,false,false};}
Fake Base(){Fake io;io.usb[Root+"/a.bin"]=B("first");io.usb[Root+"/dir/b.bin"]=B("second");io.usb[Root+"/.nexus-mod-id"]=Bytes(16,0x42);io.sd["atmosphere/package3"]=B("keep firmware");io.sd["switch/usbloader/USBLoader.nro"]=B("keep app");return io;}
void LiveEqual(const Fake& a,const Fake& b){for(auto& [k,v]:a.sd)if(!k.starts_with("switch/usbloader/mod-backups/"))assert(b.sd.contains(k)&&b.sd.at(k)==v);for(auto& [k,v]:b.sd)if(!k.starts_with("switch/usbloader/mod-backups/"))assert(a.sd.contains(k)&&a.sd.at(k)==v);}
int main(int argc,char** argv){
    if(argc==3){ // metadata-only fixture from real drive: no game payload is read
        std::ifstream f(argv[1],std::ios::binary);std::string raw((std::istreambuf_iterator<char>(f)),{});auto d=yyjson_read(raw.data(),raw.size(),0);assert(d);auto r=yyjson_doc_get_root(d);std::string root=yyjson_get_str(yyjson_obj_get(r,"root")),title=yyjson_get_str(yyjson_obj_get(r,"title"));auto a=yyjson_obj_get(r,"entries");size_t i,n;yyjson_val* v;Fake io;
        yyjson_arr_foreach(a,i,n,v){auto p=root+"/"+yyjson_get_str(yyjson_obj_get(v,"path"));if(yyjson_get_bool(yyjson_obj_get(v,"directory")))io.dirs.insert(p);else io.sizes[p]=yyjson_get_uint(yyjson_obj_get(v,"size"));}
        auto tok=std::string(yyjson_get_str(yyjson_obj_get(r,"token")));Bytes token;for(size_t i=0;i<tok.size();i+=2)token.push_back(std::stoul(tok.substr(i,2),nullptr,16));yyjson_doc_free(d);
        Bytes index;uint64_t bytes{};size_t count{};assert(BuildIndex(io,root,title,token,index,bytes,count));std::ofstream out(argv[2],std::ios::binary);out.write((char*)index.data(),index.size());assert(out.good());std::cout<<count<<" files; "<<bytes<<" bytes; index "<<index.size()<<"\n";return 0;
    }
    assert(!Title("0100000000000001")&&Title(Id)&&!Title("0600000000000000"));
    auto original=Base();std::vector<Candidate> candidates;assert(Scan(original,"/atmosphere",candidates)&&candidates.size()==1);Plan plan;assert(Prepare(original,Game(),false,plan)&&plan.file_count==2&&plan.data_bytes==11);assert(original.sd.size()==2);
    assert(Apply(original,plan)&&original.sd.contains(Index)&&original.sd.contains(Journal)&&!original.sd.contains(Pending));
    Plan unchanged;assert(Prepare(original,Game(),false,unchanged)&&unchanged.changes.empty());
    // Interoperate with the actual PC tool's original M1 ownership schema.
    auto legacy=original;
    legacy.sd[Journal]=B("{\"format\":\"nexus-usb-mods-m1\",\"title\":\""+Id+"\",\"usb_romfs\":\""+Root+"\",\"index_sha256\":\""+legacy.Hash(legacy.sd[Index])+"\",\"files\":[{\"path\":\""+Index+"\",\"sha256\":\""+legacy.Hash(legacy.sd[Index])+"\",\"size\":"+std::to_string(legacy.sd[Index].size())+",\"owned\":true}]}");
    Plan migration;assert(Prepare(legacy,Game(),false,migration)&&migration.changes.size()==1&&migration.changes[0].path==Journal);assert(Apply(legacy,migration)&&legacy.sd[Index]==original.sd[Index]);
    auto edited=original;edited.usb[Root+"/added.bin"]=B("new file");Plan fresh;assert(Prepare(edited,Game(),false,fresh));auto success=edited;assert(Apply(success,fresh));const auto steps=success.mutations-edited.mutations;
    // Interrupt BEFORE every publication/mutation; each case must either be
    // unchanged or recover to the exact prior registration and unrelated files.
    for(int step=1;step<=steps;++step){auto x=edited;auto p=fresh;x.fail_at=x.mutations+step;assert(!Apply(x,p));x.Resume();
        const auto stopped=x;const auto before=x.mutations;assert(Recover(x));const auto recovery_steps=x.mutations-before;
        LiveEqual(edited,x);assert(x.usb==edited.usb);
        // A second power/IO failure during rollback is itself resumable.
        for(int j=1;j<=recovery_steps;++j){auto y=stopped;y.fail_at=y.mutations+j;assert(!Recover(y));y.Resume();assert(Recover(y));LiveEqual(edited,y);}
    }
    // First registration rollback (no prior index/journal) and marker retention.
    auto first=Base();first.usb.erase(Root+"/.nexus-mod-id");Plan fp;assert(Prepare(first,Game(),false,fp));auto fsuccess=first;assert(Apply(fsuccess,fp));
    for(int step=1;step<=fsuccess.mutations;++step){auto x=first;auto p=fp;x.fail_at=step;assert(!Apply(x,p));x.Resume();assert(Recover(x));LiveEqual(first,x);}
    // Conflicts, stale media, SD changes after review, unsupported system IDs.
    auto bad=Base();bad.sd[Index]=B("someone else's index");assert(!Prepare(bad,Game(),false,plan));assert(bad.sd[Index]==B("someone else's index"));
    bad=Base();bad.sd["atmosphere/contents/"+Id+"/romfs/user.txt"]=B("user mod");assert(!Prepare(bad,Game(),false,plan));
    bad=Base();assert(Prepare(bad,Game(),false,plan));bad.sd[Index]=B("concurrent edit");assert(!Apply(bad,plan));
    bad=Base();assert(Prepare(bad,Game(),false,plan));bad.media=false;assert(!Apply(bad,plan));assert(!bad.sd.contains(Index));
    bad=Base();auto system=Game();system.id="0100000000000001";assert(!Prepare(bad,system,false,plan));
    // Reject malformed marker, paths, oversize/deep tables, and bad ExeFS/IPS.
    bad=Base();bad.usb[Root+"/.nexus-mod-id"]=B("bad");assert(!Prepare(bad,Game(),false,plan));
    bad=Base();bad.usb[Root+"/../oops"]=B("oops");assert(!Prepare(bad,Game(),false,plan));
    bad=Base();for(int i=0;i<1536;++i)bad.usb[Root+"/x"+std::to_string(i)]=B("x");assert(!Prepare(bad,Game(),false,plan));
    bad=Base();std::string deep=Root;for(int i=0;i<25;++i)deep+="/d";bad.usb[deep+"/file"]=B("x");assert(!Prepare(bad,Game(),false,plan));
    auto exe=Game();exe.exefs=true;bad=Base();bad.usb[exe.path+"/exefs/main"]=B("wrong");assert(!Prepare(bad,exe,true,plan));bad.usb[exe.path+"/exefs/main"]=B("NSO0test");assert(Prepare(bad,exe,true,plan)&&Apply(bad,plan));
    assert(bad.sd.at("atmosphere/contents/"+Id+"/usb_exefs.enabled")==B("NXUEXE1\n"));
    bad=Base();const auto patch="/atmosphere/exefs_patches/example/0123456789ABCDEF.ips";bad.usb[patch]=B("PATCHEOF");assert(Scan(bad,"/atmosphere",candidates)&&candidates.size()==2);assert(Prepare(bad,candidates[1],false,plan)&&Apply(bad,plan));
    bad.usb[patch]=B("PATCHbroken");assert(!Prepare(bad,candidates[1],false,plan));
    // Recovery never overwrites an unrelated edit, even if a transaction exists.
    bad=edited;bad.fail_at=bad.mutations+steps-1;auto p=fresh;assert(!Apply(bad,p));bad.Resume();assert(bad.sd.contains(Pending));bad.sd[Index]=B("user edit after interruption");const auto snapshot=bad.sd;assert(!Recover(bad)&&bad.sd==snapshot);
    // Tampered recovery paths must fail before any mutation.
    bad=Base();bad.sd[Pending]=B("{\"format\":1,\"key\":\""+Id+"\",\"backup\":\"switch/usbloader/mod-backups/"+std::string(32,'a')+"\",\"changes\":[{\"path\":\"atmosphere/package3\",\"old\":\"\",\"new\":\""+std::string(64,'a')+"\"}]}");assert(!Recover(bad)&&bad.mutations==0);
    std::cout<<"PASS: index/refresh, every transaction mutation fault, recovery, conflicts, limits, ExeFS/IPS, media pin and system-file exclusion\n";
}
