// SPDX-License-Identifier: GPL-3.0-or-later
#include "../extras/shared/nexus_boot_profile.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
std::string Pack(unsigned edition){
    std::string text="[config]\nautoboot=0\n";
    for(unsigned kind=1;kind<=2;++kind){
        text+="[CFW "+std::to_string(kind)+"]\npkg3=atmosphere/package3\n";
        text+=kind==1?"emummcforce=1\n":"emummc_force_disable=1\n";
        text+="kip1=atmosphere/kips/nexus-loader.kip\n";
        if(edition!=1)text+="kip1=atmosphere/kips/hoc.kip\nsecmon=atmosphere/exosphere.bin\n";
        text+="icon="+std::string(edition==3?(kind==1?NEXUS_EMU_ICON:NEXUS_SYS_ICON):"bootloader/res/icon_payload.bmp")+"\n";
    }return text+"[Stock]\npkg3=atmosphere/package3\nstock=1\nemummc_force_disable=1\nicon=original-stock.bmp\n";
}
std::string Merge(const std::string& from,const std::string& to){
    std::vector<char> out(from.size()*3+16384);size_t used{};unsigned changed{};
    assert(nexus_merge_boot_profile(from.data(),from.size(),to.data(),to.size(),out.data(),out.size(),&used,&changed));
    assert(changed==2);return {out.data(),used};
}
int main(){
    unsigned checks{};
    for(unsigned from=1;from<=3;++from)for(unsigned to=1;to<=3;++to){
        auto original=Pack(from);original.insert(original.find("[CFW 1]"),"# User settings\n");
        original.insert(original.find("icon="),"kip1=atmosphere/kips/custom.kip\n");
        const auto merged=Merge(original,Pack(to));
        assert(merged.find("custom.kip")!=merged.npos&&merged.find("# User settings")!=merged.npos);
        assert((merged.find("hoc.kip")!=merged.npos)==(to!=1));
        assert((merged.find("secmon=")!=merged.npos)==(to!=1));
        assert((merged.find(NEXUS_EMU_ICON)!=merged.npos)==(to==3));
        assert(merged.substr(merged.find("[Stock]"))==original.substr(original.find("[Stock]")));
        assert(Merge(merged,Pack(to))==merged);++checks;
    }
    auto source=Pack(1);source.insert(source.find("icon="),"kip1=atmosphere/kips/*.kip\n");auto merged=Merge(source,Pack(2));
    const auto first=merged.substr(0,merged.find("[CFW 2]"));assert(first.find("nexus-loader.kip")==first.npos&&first.find("hoc.kip")==first.npos&&first.find("*.kip")!=first.npos);
    source=Pack(1);source.insert(source.find("icon="),"secmon=custom-monitor.bin\n");std::vector<char> out(5000);size_t used{};unsigned changed{};
    assert(!nexus_merge_boot_profile(source.data(),source.size(),Pack(2).data(),Pack(2).size(),out.data(),out.size(),&used,&changed));
    source=Pack(1);assert(!nexus_merge_boot_profile(source.data(),source.size(),Pack(2).data(),Pack(2).size(),out.data(),10,&used,&changed));
    const std::string payload="[CFW]\npayload=bootloader/payloads/fusee.bin\n";
    assert(!nexus_merge_boot_profile(payload.data(),payload.size(),Pack(2).data(),Pack(2).size(),out.data(),out.size(),&used,&changed));
    printf("PASS: %u edition transitions, idempotence, custom KIPs, stock preservation, wildcard and incompatible-config refusal\n",checks);
}
