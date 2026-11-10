#include "nexus/library.hpp"
#include <array>
#include <vector>
#include <cassert>
#include <iostream>
#include <random>
using namespace nexus::library;
template<class T> void Put(std::vector<std::uint8_t>& b,size_t at,T value){assert(Range(at,sizeof(T),b.size()));std::memcpy(b.data()+at,&value,sizeof(T));}
int main(){
    assert(Range(0,0,0));assert(!Range(UINT64_MAX,2,UINT64_MAX));
    std::vector<std::uint8_t> h(128);Assets a{};
    assert(!Header(h,1000,a));std::memcpy(h.data()+16,"NRO0",4);Put<std::uint32_t>(h,24,4096);
    assert(Header(h,65536,a));assert(a.code==4096);assert(!Header(h,4095,a));
    std::vector<std::uint8_t> aset(56);std::memcpy(aset.data(),"ASET",4);
    Put<std::uint64_t>(aset,8,56);Put<std::uint64_t>(aset,16,100);
    Put<std::uint64_t>(aset,24,156);Put<std::uint64_t>(aset,32,0x4000);
    assert(AssetHeader(aset,65536,a));assert(a.icon==4152 && a.nacp==4252);
    Put<std::uint64_t>(aset,8,UINT64_MAX);assert(!AssetHeader(aset,65536,a));
    Put<std::uint64_t>(aset,8,56);Put<std::uint64_t>(aset,16,2*1024*1024);assert(!AssetHeader(aset,65536,a));
    std::vector<std::uint8_t> romfs(1024);Put<std::uint64_t>(romfs,0,80);Put<std::uint64_t>(romfs,56,80);
    Put<std::uint64_t>(romfs,64,44);Put<std::uint64_t>(romfs,72,512);
    Put<std::uint32_t>(romfs,80+28,11);std::memcpy(romfs.data()+112,"nextNroPath",11);
    const std::string target="sdmc:/switch/nfsmw-nx/nfsmw-nx.nro";
    Put<std::uint64_t>(romfs,80+16,target.size());std::memcpy(romfs.data()+512,target.data(),target.size());
    assert(ForwarderPath(romfs)==target);
    assert(UsbForwarderPath(romfs).empty()); // SD forwarder alone is not USB
    const auto ordinary=romfs;
    const std::string usb="/switch/nfsmw-nx/nfsmw-nx.nro";
    Put<std::uint64_t>(romfs,64,132);
    Put<std::uint32_t>(romfs,124+28,12);std::memcpy(romfs.data()+156,"nexusUsbPath",12);
    Put<std::uint64_t>(romfs,124+8,100);Put<std::uint64_t>(romfs,124+16,usb.size());std::memcpy(romfs.data()+612,usb.data(),usb.size());
    Put<std::uint32_t>(romfs,168+28,12);std::memcpy(romfs.data()+200,"nexusNroHash",12);
    Put<std::uint64_t>(romfs,168+8,200);Put<std::uint64_t>(romfs,168+16,32);romfs[712]=1;
    assert(UsbForwarderPath(romfs)==usb);
    romfs[712]=0;assert(UsbForwarderPath(romfs).empty());romfs[712]=1;
    romfs[613]='x';assert(UsbForwarderPath(romfs).empty());romfs[613]='s';
    Put<std::uint64_t>(romfs,168+16,31);assert(UsbForwarderPath(romfs).empty());
    romfs=ordinary;
    Put<std::uint32_t>(romfs,80,1);assert(ForwarderPath(romfs).empty());Put<std::uint32_t>(romfs,80,0);
    Put<std::uint64_t>(romfs,88,UINT64_MAX);assert(ForwarderPath(romfs).empty());Put<std::uint64_t>(romfs,88,0);
    romfs[520]=0;assert(ForwarderPath(romfs).empty());
    std::mt19937 rng(600);for(int i=0;i<20000;++i){std::vector<std::uint8_t> b(rng()%2048);for(auto& x:b)x=rng();
        auto path=ForwarderPath(b);assert(path.empty());assert(UsbForwarderPath(b).empty());Header(b,rng(),a);AssetHeader(b,rng(),a);}
    std::cout<<"NRO bounds, asset size limits, forwarder RomFS recognition and 20000 malformed inputs passed\n";
}
