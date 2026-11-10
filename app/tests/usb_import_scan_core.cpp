// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_import_scan_core.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace nexus::usb_import;
using Bytes=std::vector<std::uint8_t>;
template<class T> void Put(Bytes& b,size_t at,T value){assert(Range(at,sizeof(value),b.size()));std::memcpy(b.data()+at,&value,sizeof(value));}
Bytes Cnmt(std::uint8_t type=0x80,std::uint16_t count=1){
    const size_t extended=type==0x81?24:type==0x84?32:16;
    Bytes b(32+extended+count*56+32);Put<std::uint64_t>(b,0,0x0100000000001800);Put<std::uint32_t>(b,8,65536);b[12]=type;
    Put<std::uint16_t>(b,14,extended);Put<std::uint16_t>(b,16,count);
    if(type!=0x80)Put<std::uint64_t>(b,type==0x84?40:32,0x0100000000001000);
    for(size_t i=0;i<count;++i){const auto at=32+extended+i*56;b[at]=b[at+32]=i+1;Put<std::uint32_t>(b,at+48,0x4000);b[at+54]=1;}
    return b;
}
Bytes Pfs(std::vector<std::string> names,std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges){
    size_t strings{};for(const auto& name:names)strings+=name.size()+1;
    Bytes b(16+names.size()*24+strings);std::memcpy(b.data(),"PFS0",4);Put<std::uint32_t>(b,4,names.size());Put<std::uint32_t>(b,8,strings);
    size_t string_at{};for(size_t i=0;i<names.size();++i){Put<std::uint64_t>(b,16+i*24,ranges[i].first);Put<std::uint64_t>(b,24+i*24,ranges[i].second);Put<std::uint32_t>(b,32+i*24,string_at);std::memcpy(b.data()+16+names.size()*24+string_at,names[i].c_str(),names[i].size()+1);string_at+=names[i].size()+1;}return b;
}
int main(){
    Meta meta;std::string reason;auto base=Cnmt();assert(ParseCnmt(base,meta,reason));assert(meta.id==0x0100000000001800&&meta.application_id==meta.id&&meta.content.size()==1&&meta.content[0].size==0x4000&&meta.tail.empty());
    for(auto type:{0x81,0x82,0x84}){auto b=Cnmt(type);assert(ParseCnmt(b,meta,reason));assert(meta.application_id==0x0100000000001000);}
    auto bad=base;bad.resize(31);assert(!ParseCnmt(bad,meta,reason)&&!reason.empty());
    bad=base;bad[12]=0x01;assert(!ParseCnmt(bad,meta,reason));
    bad=base;bad[13]=1;assert(!ParseCnmt(bad,meta,reason));
    bad=base;bad[22]=1;assert(!ParseCnmt(bad,meta,reason));
    bad=base;Put<std::uint16_t>(bad,14,8);assert(!ParseCnmt(bad,meta,reason));
    bad=base;Put<std::uint16_t>(bad,16,0xffff);assert(!ParseCnmt(bad,meta,reason));
    bad=base;Put<std::uint16_t>(bad,18,0xffff);assert(!ParseCnmt(bad,meta,reason));
    bad=base;bad.resize(60);assert(!ParseCnmt(bad,meta,reason));
    bad=Cnmt(0x80,2);bad[48+56]=bad[48+56+32]=1;assert(!ParseCnmt(bad,meta,reason));
    bad=base;bad[48]=2;assert(!ParseCnmt(bad,meta,reason));
    bad=base;Put<std::uint32_t>(bad,48+48,0xBFF);assert(!ParseCnmt(bad,meta,reason));
    bad=base;bad[48+54]=0;assert(!ParseCnmt(bad,meta,reason));
    bad=base;Put<std::uint64_t>(bad,0,0);assert(!ParseCnmt(bad,meta,reason));
    bad=Cnmt(0x84);Put<std::uint64_t>(bad,40,0);assert(!ParseCnmt(bad,meta,reason));
    bad=Cnmt(0x81);Put<std::uint32_t>(bad,44,1024);assert(!ParseCnmt(bad,meta,reason));
    auto tail=Cnmt(0x81);Put<std::uint16_t>(tail,18,1);Put<std::uint32_t>(tail,44,16);tail.insert(tail.end()-32,32,0xA5);assert(ParseCnmt(tail,meta,reason)&&meta.tail.size()==32&&meta.tail.front()==0xA5);
    bad=base;bad.push_back(0);assert(!ParseCnmt(bad,meta,reason));
    auto no_digest=base;no_digest.resize(no_digest.size()-32);assert(ParseCnmt(no_digest,meta,reason));
    std::array<std::uint8_t,16> id{};assert(ContentName("0123456789abcdef0123456789abcdef.nca",id));assert(!ContentName("../0123456789abcdef0123456789abcdef.nca",id));assert(!ContentName("00000000000000000000000000000000.nca",id));assert(!ContentName("0123456789abcdef0123456789abcdeg.nca",id));
    assert(Utf8("Game title")&&Utf8("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"));assert(!Utf8("\xC0\xAF")&&!Utf8("\xED\xA0\x80")&&!Utf8("\xF4\x90\x80\x80")&&!Utf8("\xE3\x83"));
    std::uint64_t offset{},size{};auto pfs=Pfs({"Application_0100000000001800.cnmt"},{{0,base.size()}});assert(FindCnmt(pfs,pfs.size()+base.size(),offset,size)&&offset==pfs.size()&&size==base.size());
    auto pbad=pfs;pbad.back()='X';assert(!FindCnmt(pbad,pbad.size()+base.size(),offset,size));
    pbad=pfs;Put<std::uint32_t>(pbad,4,0xffffffff);assert(!FindCnmt(pbad,std::numeric_limits<std::uint64_t>::max(),offset,size));
    pbad=pfs;Put<std::uint64_t>(pbad,16,std::numeric_limits<std::uint64_t>::max());assert(!FindCnmt(pbad,pbad.size()+base.size(),offset,size));
    pbad=Pfs({"a.cnmt","b.cnmt"},{{0,64},{64,64}});assert(!FindCnmt(pbad,pbad.size()+128,offset,size));
    pbad=Pfs({"a.cnmt","other"},{{0,64},{32,64}});assert(!FindCnmt(pbad,pbad.size()+128,offset,size));
    pbad=Pfs({"../a.cnmt"},{{0,64}});assert(!FindCnmt(pbad,pbad.size()+64,offset,size));
    pbad=Pfs({"a.cnmt","other"},{{0,64},{64,64}});assert(FindCnmt(pbad,pbad.size()+128,offset,size));
    Bytes romfs(44);Put<std::uint64_t>(romfs,8,128);Put<std::uint64_t>(romfs,16,0x4000);Put<std::uint32_t>(romfs,28,12);std::memcpy(romfs.data()+32,"control.nacp",12);
    assert(FindRomfsRootFile(romfs,0x4080,"control.nacp",offset,size)&&offset==128&&size==0x4000);
    assert(!FindRomfsRootFile(romfs,0x407f,"control.nacp",offset,size));
    auto rbad=romfs;Put<std::uint32_t>(rbad,28,0xffffffff);assert(!FindRomfsRootFile(rbad,0x4080,"control.nacp",offset,size));
    rbad=romfs;Put<std::uint64_t>(rbad,8,std::numeric_limits<std::uint64_t>::max());assert(!FindRomfsRootFile(rbad,0x4080,"control.nacp",offset,size));
    rbad=romfs;rbad.insert(rbad.end(),romfs.begin(),romfs.end());assert(!FindRomfsRootFile(rbad,0x4080,"control.nacp",offset,size));
    rbad=romfs;Put<std::uint32_t>(rbad,0,8);assert(!FindRomfsRootFile(rbad,0x4080,"control.nacp",offset,size));
    assert(!Range(std::numeric_limits<std::uint64_t>::max()-8,16,std::numeric_limits<std::uint64_t>::max()));
    // Exercise production bounds under sanitizers with reproducible malformed
    // inputs, including structured inputs that reach beyond the first guard.
    std::uint32_t random=0x9136A765;
    const auto next=[&](){random=random*1664525+1013904223;return random;};
    for(size_t i=0;i<10000;++i){
        auto candidate=(i&1)?base:Cnmt(0x84,3);
        for(size_t n=0;n<1+i%8;++n)candidate[next()%candidate.size()]=next()&255;
        if(i%3==0)candidate.resize(next()%candidate.size());
        if(ParseCnmt(candidate,meta,reason))assert(meta.content.size()<=MaxContent&&meta.extended.size()<=4096);
        auto random_pfs=pfs;for(size_t n=0;n<4;++n)random_pfs[next()%random_pfs.size()]=next()&255;
        (void)FindCnmt(random_pfs,random_pfs.size()+base.size(),offset,size);
        auto random_romfs=romfs;for(size_t n=0;n<4;++n)random_romfs[next()%random_romfs.size()]=next()&255;
        (void)FindRomfsRootFile(random_romfs,0x4080,"control.nacp",offset,size);
    }
    std::cout<<"USB import metadata parser: all cases passed\n";
}
