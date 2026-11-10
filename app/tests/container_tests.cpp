// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the real PFS0/HFS0 parsers against a bounded, synthetic host source.
// Fixtures contain no actual game content, keys or tickets.
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"
#include "nexus/package.hpp"
#include "defines.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>
using namespace sphaira::yati;
using namespace sphaira::yati::container;
using Bytes=std::vector<u8>;
struct Memory final : source::Base {
    Bytes bytes;
    s64 short_at{-1},zero_at{-1},error_at{-1};
    bool shorted{},failed{},outside{};
    std::vector<std::pair<s64,s64>> reads;
    explicit Memory(Bytes b):bytes(std::move(b)){}
    Result Read(void* dst,s64 off,s64 size,u64* got) override {
        *got=0;
        reads.emplace_back(off,size);
        if(failed||off==error_at)return 99;
        if(off<0 || size<0 || static_cast<u64>(off)>bytes.size()) {outside=true;return 98;}
        u64 count=std::min<u64>(size,bytes.size()-off);
        if(off==zero_at)count=0;
        if(off==short_at && count) {--count;shorted=true;}
        if(count)std::memcpy(dst,bytes.data()+off,count);
        *got=count;
        return 0;
    }
};
template<class T> void Put(Bytes& b,std::size_t off,T value) {
    assert(off+sizeof(T)<=b.size());std::memcpy(b.data()+off,&value,sizeof(T));
}
template<class T> T Get(const Bytes& b,std::size_t off) {
    assert(off+sizeof(T)<=b.size());T value;std::memcpy(&value,b.data()+off,sizeof(T));return value;
}
using File=std::pair<std::string,Bytes>;
Bytes Hfs(const std::vector<File>& files) {
    Xci::Hfs0Header h{0x30534648,static_cast<u32>(files.size()),0,0};
    for(const auto& f:files)h.string_table_size+=f.first.size()+1;
    const auto names=sizeof(h)+files.size()*sizeof(Xci::Hfs0FileTableEntry);
    const auto data=names+h.string_table_size;
    Bytes b(data);Put(b,0,h);
    u32 name_offset{};u64 data_offset{};
    for(std::size_t i=0;i<files.size();++i) {
        const auto& [name,payload]=files[i];
        Xci::Hfs0FileTableEntry entry{};
        entry.name_offset=name_offset;entry.data_offset=data_offset;entry.data_size=payload.size();
        Put(b,sizeof(h)+i*sizeof(entry),entry);
        std::memcpy(b.data()+names+name_offset,name.c_str(),name.size()+1);
        name_offset+=name.size()+1;data_offset+=payload.size();
        b.insert(b.end(),payload.begin(),payload.end());
    }
    return b;
}
Bytes Secure(bool compressed=false) {
    return Hfs({{"base.cnmt.nca",Bytes(12,0x31)},
        {compressed?"program.ncz":"program.nca",Bytes(20,0x42)},
        {"application.tik",Bytes(8,0x53)}});
}
Bytes Cartridge(Bytes root,std::size_t offset=0xf000) {
    Bytes b(offset);b.insert(b.end(),root.begin(),root.end());return b;
}
Bytes XciFixture(bool compressed=false,std::size_t offset=0xf000) {
    // Non-game partition deliberately not a nested HFS0. Install must not read it.
    return Cartridge(Hfs({{"secure",Secure(compressed)},{"update",Bytes(40,0xad)}}),offset);
}
Bytes NspFixture(bool compressed=false) {
    Collections entries{{"base.cnmt.nca",0,12},{compressed?"program.ncz":"program.nca",0,20}};
    s64 total{};auto b=Nsp::Build(entries,total);b.resize(total,0x5a);return b;
}
Result ParseXci(Bytes b,s64 size=-2) {
    Memory m(std::move(b));Xci p(&m,size==-2?static_cast<s64>(m.bytes.size()):size);
    Collections out;const auto rc=p.GetCollections(out);assert(!m.outside);return rc;
}
Result ParseNsp(Bytes b,s64 size=-2) {
    Memory m(std::move(b));Nsp p(&m,size==-2?static_cast<s64>(m.bytes.size()):size);
    Collections out;const auto rc=p.GetCollections(out);assert(!m.outside);return rc;
}
int main() {
    using namespace nexus::package;
    static_assert(Classify("/Game.XcI")==Kind::Xci);
    for(const auto& [ext,kind]:std::vector<std::pair<std::string,Kind>>{
        {"nsp",Kind::Nsp},{"nsz",Kind::Nsz},{"xci",Kind::Xci},{"xcz",Kind::Xcz}}) {
        for(unsigned mask=0;mask<8;++mask) {
            auto e=ext;for(unsigned i=0;i<3;++i)if(mask&(1u<<i))e[i]-=('a'-'A');
            assert(Classify("/Games/Test."+e)==kind);
        }
    }
    for(const auto* bad:{"","a.nsp.bak","a.xc0","a.ns0","a.nsp.00","a.nro","a.nca",
        "a.zip","a.rar","a.7z","a.msp","folder.xci/file","a.nsp/","a.nsp\\b"})assert(!Supported(bad));
    for(bool compressed:{false,true}) {
        for(std::size_t off:{0xf000,0x10000}) {
            Memory m(XciFixture(compressed,off));Xci p(&m,m.bytes.size());Collections out;
            assert(!p.GetCollections(out) && out.size()==3);
            assert(out[0].name=="base.cnmt.nca" && out[0].size==12);
            assert(out[1].name==(compressed?"program.ncz":"program.nca") && out[1].size==20);
            assert(m.bytes.at(out[0].offset)==0x31 && m.bytes.at(out[1].offset)==0x42);
            assert(!p.GetCollections(out) && out.size()==3); // repeated parsing does not append
            assert(!m.outside);
        }
        Memory m(NspFixture(compressed));Nsp p(&m,m.bytes.size());Collections out;
        assert(!p.GetCollections(out) && out.size()==2);
        assert(out[1].name==(compressed?"program.ncz":"program.nca"));
        assert(out[1].offset+out[1].size==static_cast<s64>(m.bytes.size()));
        assert(!p.GetCollections(out) && out.size()==2);
        assert(ParseNsp(m.bytes,-1)==0 && ParseXci(XciFixture(compressed),-1)==0);
    }
    { // GetRoot still supports callers needing all partitions.
        Memory m(Cartridge(Hfs({{"secure",Secure()},{"update",Hfs({})}})));
        Xci p(&m,m.bytes.size());Xci::Root out{};
        assert(!p.GetRoot(out) && out.partitions.size()==2 && out.partitions[0].collections.size()==3);
        assert(!p.GetRoot(out) && out.partitions.size()==2);
    }
    assert(ParseXci(Cartridge(Hfs({{"normal",Secure()}})))==Result_XciSecurePartitionNotFound);
    assert(ParseXci(Cartridge(Hfs({{"secure",Secure()},{"secure",Secure()}})))==Result_XciInvalidHeader);
    const auto original=XciFixture();
    for(auto length:{std::size_t(0),std::size_t(0xefff),std::size_t(0xf007),original.size()-1}) {
        auto b=original;b.resize(length);assert(ParseXci(b)!=0);
    }
    constexpr std::size_t root=0xf000,entry=root+16;
    auto rejectXci=[&](std::size_t pos,auto value){auto b=original;Put(b,pos,value);assert(ParseXci(b)!=0);};
    rejectXci(root+4,u32(65537));rejectXci(root+8,u32(16*1024*1024+1));
    rejectXci(entry+16,u32(9999));rejectXci(entry,u64(UINT64_MAX));
    rejectXci(entry+8,u64(UINT64_MAX));rejectXci(entry+8,u64(8)); // nested HFS smaller than its header
    const auto rootData=root+16+2*64+Get<u32>(original,root+8);
    rejectXci(rootData,u32(0)); // bad nested magic
    rejectXci(rootData+16+8,u64(original.size())); // content must fit its secure partition
    rejectXci(rootData+16,u64(INT64_MAX));
    {auto b=original;std::fill(b.begin()+root+16+2*64,b.begin()+rootData,'X');assert(ParseXci(b)!=0);}
    {auto b=original;b[root+16+2*64]=0;assert(ParseXci(b)!=0);}
    { // Positive short reads are filled at the next offset; zero/error never retry.
        Memory baseline(original);Xci p(&baseline,original.size());Collections out;assert(!p.GetCollections(out));
        for(const auto& [off,size]:baseline.reads)if(size) {
            Memory m(original);m.short_at=off;Xci q(&m,original.size());
            assert((q.GetCollections(out)==0)==(size>1));assert(m.shorted);
            Memory zero(original);zero.zero_at=off;Xci z(&zero,original.size());assert(z.GetCollections(out)!=0);
            Memory error(original);error.error_at=off;Xci e(&error,original.size());assert(e.GetCollections(out)==99);
        }
        Memory m(original);m.failed=true;Xci q(&m,original.size());assert(q.GetCollections(out)==99);
    }
    const auto nsp=NspFixture();
    for(auto length:{std::size_t(0),std::size_t(8),nsp.size()-1}) {
        auto b=nsp;b.resize(length);assert(ParseNsp(b)!=0);
    }
    auto rejectNsp=[&](std::size_t pos,auto value){auto b=nsp;Put(b,pos,value);assert(ParseNsp(b)!=0);};
    rejectNsp(0,u32(0));rejectNsp(4,u32(65537));rejectNsp(8,u32(16*1024*1024+1));
    rejectNsp(16,u64(UINT64_MAX));rejectNsp(24,u64(UINT64_MAX));rejectNsp(32,u32(9999));
    {auto b=nsp;std::fill(b.begin()+16+2*24,b.begin()+16+2*24+Get<u32>(b,8),'X');assert(ParseNsp(b)!=0);}
    {
        Memory baseline(nsp);Nsp p(&baseline,nsp.size());Collections out;assert(!p.GetCollections(out));
        for(const auto& [off,size]:baseline.reads)if(size) {
            Memory m(nsp);m.short_at=off;Nsp q(&m,nsp.size());assert((q.GetCollections(out)==0)==(size>1));assert(m.shorted);
            Memory zero(nsp);zero.zero_at=off;Nsp z(&zero,nsp.size());assert(z.GetCollections(out)!=0);
            Memory error(nsp);error.error_at=off;Nsp e(&error,nsp.size());assert(e.GetCollections(out)==99);
        }
        Memory m(nsp);m.failed=true;Nsp q(&m,nsp.size());assert(q.GetCollections(out)==99);
    }
    std::cout<<"Container tests passed: real PFS0/HFS0 parsers, 4 format classifications, bounds, short reads, secure-only extraction.\n";
}
