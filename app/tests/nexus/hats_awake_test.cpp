// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/awake_policy.hpp"
#include "nexus/hats_catalog.hpp"
#include "nexus/hats_shared/hats_contents_policy.hpp"
#include <cassert>
#include <cstdio>
#include <memory>
#include <fstream>
int main(int argc,char** argv){
    nexus::power::AwakePolicy awake;
    assert(!awake.Disabled());awake.Focus(true);assert(awake.Disabled());
    // Closing progress/FTP never relinquishes foreground ownership.
    for(int i=0;i<100;++i)assert(awake.Disabled());
    awake.Focus(false);assert(!awake.Disabled());awake.Focus(true);assert(awake.Disabled());
    awake.Stop();assert(!awake.Disabled());awake.Focus(true);assert(!awake.Disabled());
    using namespace nexus::hats;
    assert(core::InstalledTag("Nexus HATS - Atmosphere 1.12.2 RF3\nBuilt today")=="v1.12.2-rf3");
    assert(core::InstalledTag("Unknown firmware").empty());
    for(const char* p:{"../atmosphere/package3","/atmosphere/package3","sdmc:/a","a/../../b","a//b","a/./b","a\\b","a/../","a\nB","a./b"})assert(!core::Path(p));
    for(const char* p:{"atmosphere/package3","bootloader/hekate_ipl.ini","switch/","switch/nexusmanager/NexusManager.nro"})assert(core::Path(p));
    Pack p;p.tag_name="v1.12.2-rf3";p.asset_name="Nexus-1.12.2-RF3-Standard-HOC.zip";
    p.download_url="https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases/download/"+p.tag_name+"/"+p.asset_name;
    p.asset_digest="sha256:"+std::string(64,'a');p.size=31000000;p.asset_id=4;assert(ValidPack(p));
    auto evil=p;evil.download_url="https://example.com/pack.zip";assert(!ValidPack(evil));
    evil=p;evil.asset_name="../pack.zip";assert(!ValidPack(evil));evil=p;evil.asset_digest.clear();assert(!ValidPack(evil));
    evil=p;evil.prerelease=true;assert(!ValidPack(evil));evil=p;evil.size=129*1024*1024;assert(!ValidPack(evil));
    const auto release=[&](const char* tag,const char* name){return std::string("{\"tag_name\":\"")+tag+"\",\"assets\":[{\"id\":4,\"name\":\""+name+"\",\"size\":31000000,\"digest\":\""+p.asset_digest+"\",\"browser_download_url\":\"https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases/download/"+tag+"/"+name+"\"}]}";};
    auto json="["+release("v1.12.2-rf3",p.asset_name.c_str())+","+release("v1.12.2-rf2","Nexus-1.12.2-RF2-Standard-HOC.zip")+"]";
    Release out;assert(Parse(json,"v1.12.2-rf2",out)&&out.newer&&out.packs.size()==1);
    assert(Parse(json,"v1.12.2-rf3",out)&&!out.newer);assert(Parse(json,"",out)&&!out.newer);
    assert(Parse(json,"v1.12.2-custom",out)&&!out.newer);assert(!Parse("{}","",out));
    const std::string identity=R"({"schema":1,"product":"Nexus HATS","sequence":1,"version":"NX1","release_tag":"vNX1","atmosphere":"1.12.2","edition":"standard-hoc"})";
    Identity id;assert(ReadIdentity(identity,id)&&id.sequence==1&&MatchesPack(id,"vNX1","Nexus-NX1-Standard-HOC.zip"));
    assert(!MatchesPack(id,"vNX2","Nexus-NX1-Standard-HOC.zip"));
    assert(!MatchesPack(id,"vNX1","Nexus-NX1-Standard-No-HOC.zip"));
    auto dup=identity;dup.insert(1,"\"sequence\":2,");assert(!ReadIdentity(dup,id));
    for(const char* bad:{"NX0","NX01","nx-1","NX1x","v1.12.2-NX1","NX99999999","NX1\n"})assert(!Sequence(bad));
    assert(Sequence("vNX10")==10&&Sequence("NX2")==2);
    for(const auto& edition:{"standard-no-hoc","standard-hoc","nexus-themed-hoc"}){
        auto text=identity;const auto at=text.find("standard-hoc");text.replace(at,12,edition);
        assert(ReadIdentity(text,id)&&id.sequence==1&&EditionCode(id.edition));
    }
    json="["+release("vNX2","Nexus-NX2-Standard-HOC.zip")+","+release("vNX10","Nexus-NX10-Standard-HOC.zip")+"]";
    assert(Parse(json,"vNX1",out)&&out.newer&&out.tag=="vNX10");
    assert(Parse(json,"vNX10",out)&&!out.newer);assert(Parse(json,"vNX11",out)&&!out.newer);
    assert(Parse(json,"",out)&&!out.newer);
    json="["+release("vNX1","Nexus-NX1-Standard-HOC.zip")+","+release("v1.12.2-rf3",p.asset_name.c_str())+"]";
    assert(Parse(json,"v1.12.2-rf3",out)&&out.newer);
    auto policy=std::make_unique<mm_contents_policy>();
    assert(argc==2);std::ifstream input(argv[1]);assert(input.good());
    std::string protections((std::istreambuf_iterator<char>(input)),{});
    assert(sphaira::ui::menu::hats::ParseGithubExclusions(*policy,protections.data(),protections.size()));
    assert(mm_policy_preserve(policy.get(),"switch/nexusmanager",sizeof("switch/nexusmanager")-1));
    assert(mm_policy_preserve(policy.get(),"Nintendo",8));assert(mm_policy_preserve(policy.get(),"emuMMC",6));
    for(const char* path:{"atmosphere/usb_content","atmosphere/usb_content/title.bin","atmosphere/usb_badges","Nintendo","emuMMC","switch/nexusmanager/NexusManager.nro"})
        assert(mm_is_protected_install_path(policy.get(),path,true));
    assert(!mm_is_protected_install_path(policy.get(),"atmosphere/nexus-pack.json",true));
    puts("PASS HATS feed, pack validation, traversal protection, library preservation and foreground sleep policy");
}
