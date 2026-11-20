// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/port_manager_core.hpp"
#include <cassert>
#include <map>
#include <iostream>
using namespace nexus::ports;
struct File {std::uint64_t size;std::string hash;auto operator<=>(const File&)const=default;};
struct IO {
    std::map<std::string,File> files;bool session{},active{};unsigned step{},fail{};std::string error;
    bool Fail(const std::string& text){error=text;return false;}
    bool Check(){return true;}
    bool Tick(){return !fail||++step!=fail;}
    bool Exists(const std::string& p,bool& value){value=files.contains(p);return true;}
    bool Match(const std::string& p,bool wanted,std::uint64_t size,const std::string& hash){return files.contains(p)==wanted&&(!wanted||files.at(p)==File{size,hash});}
    bool Parents(const std::string&){return true;}
    bool Rename(const std::string& a,const std::string& b){if(!Tick())return false;if(!files.contains(a)||files.contains(b))return false;files[b]=files.at(a);files.erase(a);return true;}
    bool Flush(){return Tick();}
    bool Begin(const ModPlan&){if(!Tick()||session)return false;session=true;return true;}
    bool Activated(){if(!Tick())return false;active=true;return true;}
    bool Finished(){if(!Tick())return false;session=active=false;return true;}
};
int main(){
    for(auto p:{"/switch/left4dead","/atmosphere/contents/0100b00b51230000","/atmosphere/contents/0100b00b51230000/romfs","/fallout1"})assert(Root(p));
    for(auto p:{"/","/switch","/atmosphere","/atmosphere/contents","/atmosphere/contents/010000000000000d","/atmosphere/package3","/bootloader","/nintendo","/registered","/switch/nexusmanager","/switch/a/../b","/switch/a\\b","/nexus-port-manager/a"})assert(!Root(p));
    assert(DefaultRoot("/switch/port/runtime/app.nro")=="/switch/port");
    assert(DefaultRoot("/switch/app.nro")=="/switch/app.nro");
    assert(ArchivePath("switch/port/data/test.bin","/switch/port")=="data/test.bin");
    assert(ArchivePath("atmosphere/contents/0100b00b51230000/romfs/a","/atmosphere/contents/0100b00b51230000")=="romfs/a");
    for(auto p:{"../a","/absolute","a/../../b","a\\b","a:stream","switch/another/a","atmosphere/package3"})assert(ArchivePath(p,"/switch/port").empty());
    const std::string oldhash(64,'a'),newhash(64,'b');
    ModPlan plan{"/switch/port","/nexus-port-manager/0123456789abcdef/0123456789abcdef","Test",{{"data/original",newhash,12,true,oldhash,5},{"data/new",newhash,8,false,{},0}},20};
    assert(Valid(plan));
    auto invalid=plan;invalid.files[1].relative="data";assert(!Valid(invalid));
    invalid=plan;invalid.files[1].relative="DATA/ORIGINAL";assert(!Valid(invalid));
    invalid=plan;invalid.files[1].relative="../../other";assert(!Valid(invalid));
    invalid=plan;invalid.root="/atmosphere";assert(!Valid(invalid));
    IO initial;initial.files[plan.root+"/data/original"]={5,oldhash};initial.files[plan.root+"/save.dat"]={9,oldhash};
    initial.files[plan.store+"/payload/data/original"]={12,newhash};initial.files[plan.store+"/payload/data/new"]={8,newhash};
    IO success=initial;assert(Enable(success,plan)&&success.active&&success.session);assert(success.files.at(plan.root+"/data/original").hash==newhash);
    auto changed=success;changed.files[plan.root+"/data/original"].hash=oldhash;auto before=changed.files;assert(!Disable(changed,plan)&&changed.files==before);
    auto missing=success;missing.files.erase(plan.store+"/original/data/original");before=missing.files;assert(!Disable(missing,plan)&&missing.files==before);
    auto bad_source=initial;bad_source.files[plan.store+"/payload/data/new"].size=99;before=bad_source.files;assert(!Enable(bad_source,plan)&&!bad_source.session&&bad_source.files==before);
    assert(Disable(success,plan)&&success.files==initial.files&&!success.session&&!success.active);
    unsigned failures{};
    // Every rename/flush/journal boundary is interrupted, then restored using
    // the actual production engine. Pre-existing saves and other files survive.
    for(unsigned at=1;at<=12;++at){auto io=initial;io.fail=at;const bool ok=Enable(io,plan);io.fail=0;if(!ok)++failures;if(io.session)assert(Disable(io,plan));assert(io.files==initial.files);}
    for(unsigned at=1;at<=12;++at){auto io=initial;assert(Enable(io,plan));io.step=0;io.fail=at;const bool ok=Disable(io,plan);io.fail=0;if(!ok)++failures;if(io.session)assert(Disable(io,plan));assert(io.files==initial.files&&!io.session);}
    assert(failures>=10);
    std::cout<<"Port/mod production engine passed: folder boundaries, ZIP paths, original restoration, edited-file conflicts, missing originals and "<<failures<<" interruption recoveries\n";
}
