#include "nexus/library.hpp"
#ifdef NDEBUG
#error "Run these tests with assertions enabled (-UNDEBUG)"
#endif
#include <vector>
#include <cassert>
#include <iostream>
using namespace nexus::library;
template<class T> void Put(std::vector<std::uint8_t>& b,size_t at,T v){assert(Range(at,sizeof(v),b.size()));std::memcpy(b.data()+at,&v,sizeof(v));}
std::vector<std::uint8_t> Rom(const std::vector<std::pair<std::string,std::string>>& entries){
    std::vector<std::uint8_t> b(8192);Put<std::uint64_t>(b,0,80);Put<std::uint64_t>(b,56,80);Put<std::uint64_t>(b,72,4096);
    size_t table=0,data=0;for(const auto& [name,value]:entries){const auto at=80+table;
        Put<std::uint64_t>(b,at+8,data);Put<std::uint64_t>(b,at+16,value.size());Put<std::uint32_t>(b,at+28,name.size());
        std::memcpy(b.data()+at+32,name.data(),name.size());std::memcpy(b.data()+4096+data,value.data(),value.size());
        table+=(32+name.size()+3)&~size_t{3};data+=value.size();}
    Put<std::uint64_t>(b,64,table);return b;
}
int main(){
    const std::string nro="sdmc:/switch/My Port/app.nro";std::string tail;bool found{};std::string value;
    auto rom=Rom({{"nextNroPath",nro},{"nextArgv","\""+nro+"\" \"sdmc:/switch/My Port/drive_c/Game.exe\" -fullscreen"}});
    assert(ForwarderArguments(rom,nro,tail)&&tail=="\"sdmc:/switch/My Port/drive_c/Game.exe\" -fullscreen");
    assert(RootText(rom,"nextNroPath",value,found)&&found&&value==nro);
    assert(!ForwarderArguments(Rom({{"nextArgv","sdmc:/switch/wrong.nro --run"}}),nro,tail));
    assert(!ForwarderArguments(Rom({{"nextArgv","\""+nro+"\"bad"}}),nro,tail));
    assert(!RootText(Rom({{"nextArgv","one"},{"nextArgv","two"}}),"nextArgv",value,found));
    assert(!RootText(Rom({{"nextNroPath",std::string("abc\0def",7)}}),"nextNroPath",value,found));
    assert(ForwarderArguments(Rom({{"nextNroPath",nro}}),nro,tail)&&tail.empty());
    std::vector<std::uint8_t> pfs(16+24+10);std::memcpy(pfs.data(),"PFS0",4);Put<std::uint32_t>(pfs,4,1);Put<std::uint32_t>(pfs,8,10);
    Put<std::uint64_t>(pfs,24,16);std::memcpy(pfs.data()+40,"main.npdm",10);std::uint64_t off{},size{};
    assert(PfsFile(pfs,66,"main.npdm",off,size)&&off==50&&size==16);
    assert(!PfsFile(pfs,65,"main.npdm",off,size));
    Put<std::uint64_t>(pfs,16,UINT64_MAX);assert(!PfsFile(pfs,66,"main.npdm",off,size));Put<std::uint64_t>(pfs,16,0);
    pfs[49]='X';assert(!PfsFile(pfs,66,"main.npdm",off,size));
    std::cout<<"Forwarder argv preservation, duplicate/malformed contracts and NPDM PFS bounds passed\n";
}
