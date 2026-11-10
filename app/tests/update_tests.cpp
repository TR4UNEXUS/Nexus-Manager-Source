// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/release.hpp"
#include <cassert>
#include <string>
#include <iostream>
#include <fstream>
#include <iterator>
using namespace nexus::updates;
std::string Entry(std::string tag="v0.3.0-preview",std::string digest=std::string(64,'a'),std::string repo=Repository,bool draft=false){
    return "{\"tag_name\":\""+tag+"\",\"draft\":"+(draft?"true":"false")+",\"prerelease\":true,\"assets\":[{\"name\":\"NexusManager.nro\",\"state\":\"uploaded\",\"size\":2000000,\"digest\":\"sha256:"+digest+"\",\"browser_download_url\":\""+repo+"/releases/download/"+tag+"/NexusManager.nro\"}]}";
}
int main(int argc,char** argv){
    assert(VersionCode("v0.3.0-preview")==3000);
    assert(VersionCode("1.0.0")>VersionCode("0.99.99"));
    for(auto bad:{"","v","1.2","1.2.3.4","01.2.3","1.2.3/../x","1.2.3-beta","1.-2.3","1.2.1000"})assert(!VersionCode(bad));
    Release out;assert(SelectRelease("["+Entry()+"]",out));assert(out.version=="0.3.0" && out.preview);
    assert(!SelectRelease("["+Entry("v0.3.0",std::string(63,'a'))+"]",out));
    assert(!SelectRelease("["+Entry("v0.3.0",std::string(64,'z'))+"]",out));
    assert(!SelectRelease("["+Entry("v0.3.0",std::string(64,'a'),"https://github.com/other/repo")+"]",out));
    assert(!SelectRelease("["+Entry("v0.3.0",std::string(64,'a'),Repository,true)+"]",out));
    assert(SelectRelease("["+Entry("v0.2.0")+","+Entry("v0.4.0-preview")+","+Entry("v0.3.0")+"]",out));
    assert(out.version=="0.4.0");
    assert(!SelectRelease("{} ",out));assert(!SelectRelease("[}",out));assert(!SelectRelease("[]",out));
    assert(!ValidNro("not an NRO","0.3.0"));
    if(argc>=2){
        std::ifstream f(argv[1],std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(f),{}};
        assert(!bytes.empty());assert(ValidNro(bytes,Version));assert(!ValidNro(bytes,"99.0.0"));
        auto corrupt=bytes;corrupt[0x10]='X';assert(!ValidNro(corrupt,Version));
        corrupt=bytes;for(int i=0;i<4;++i)corrupt[0x18+i]=char(0xff);
        assert(!ValidNro(corrupt,Version));
        assert(!ValidNro(std::string_view(bytes).substr(0,bytes.size()/2),Version));
    }
    if(argc==3){
        std::ifstream f(argv[2]);std::string json{std::istreambuf_iterator<char>(f),{}};
        assert(SelectRelease(json,out));assert(out.version=="0.3.0");
        std::cout<<"Live public GitHub release selected: "<<out.tag<<" / "<<out.size<<" bytes\n";
    }
    std::cout<<"Update policy tests passed: versions, release selection, owner/asset binding, digests and NRO identity.\n";
}
