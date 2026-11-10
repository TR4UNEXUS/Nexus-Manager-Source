// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/installed_move_core.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

static unsigned assertions{};
static void Check(bool value){++assertions;if(!value){std::cerr<<"Failed assertion "<<assertions<<'\n';std::exit(1);}}
constexpr std::uint32_t Invalid=10,Fault=77;
struct ContentInfo{
    std::array<std::uint8_t,16> id{};
    std::uint32_t size_low{};
    std::uint8_t size_high{},attributes{},content_type{},id_offset{};
};
static_assert(sizeof(ContentInfo)==24);
static std::uint16_t Read16(const std::array<std::uint8_t,8>& h,std::size_t at){return h[at]|std::uint16_t(h[at+1])<<8;}
struct MemoryIo{
    std::array<std::uint8_t,8> header{};
    std::vector<ContentInfo> contents;
    std::uint64_t bytes{},actual_header=8;
    unsigned calls{},fail_at{},size_calls{},header_calls{},pages{},max_capacity{};
    int count_adjust{};
    bool change_size{},change_header{};
    std::uint32_t Step(){return ++calls==fail_at?Fault:0;}
    std::uint32_t Check(){return Step();}
    std::uint32_t Size(std::uint64_t& out){if(auto r=Step())return r;out=bytes+(change_size&&size_calls++?1:0);return 0;}
    std::uint32_t Header(std::array<std::uint8_t,8>& out,std::uint64_t& actual){
        if(auto r=Step())return r;
        out=header;actual=actual_header;
        if(change_header&&header_calls++)out[6]^=1;
        return 0;
    }
    std::uint32_t Infos(std::int32_t offset,ContentInfo* out,std::size_t capacity,std::int32_t& count){
        if(auto r=Step())return r;
        ++pages;max_capacity=std::max<unsigned>(max_capacity,capacity);
        ::Check(offset>=0&&std::size_t(offset)<=contents.size());
        const auto n=std::min(capacity,contents.size()-offset);
        std::copy_n(contents.begin()+offset,n,out);count=static_cast<std::int32_t>(n)+count_adjust;return 0;
    }
};
static MemoryIo Fixture(std::uint16_t count=3,std::uint16_t dependencies=1){
    MemoryIo io;io.header={16,0,std::uint8_t(count),std::uint8_t(count>>8),std::uint8_t(dependencies),std::uint8_t(dependencies>>8),0,5};
    io.bytes=8+16+std::uint64_t(count)*sizeof(ContentInfo);io.contents.resize(count);
    for(std::uint32_t i=0;i<count;++i){std::memcpy(io.contents[i].id.data(),&i,sizeof(i));io.contents[i].size_low=i+1;}
    return io;
}
static std::uint32_t Run(MemoryIo& io,std::set<std::uint32_t>& candidates,bool& deletion_gate){
    const auto rc=nexus::installed_move::VisitReferences<ContentInfo>(io,[&](const ContentInfo& item){
        std::uint32_t id{};std::memcpy(&id,item.id.data(),sizeof(id));candidates.erase(id);
    },Invalid);
    // The production caller checks this returned result before proceeding to
    // source deletion. Candidate changes are RAM-only until that gate succeeds.
    deletion_gate=rc==0;return rc;
}
int main(){
    // Real legacy-forwarder shape: 8-byte header +16-byte extension +3 NCAs
    // =96 bytes. One declared (missing) 16-byte dependency is irrelevant to
    // finding references to content IDs. The old strict full-record test fails.
    {auto io=Fixture();Check(io.bytes==96);Check(io.bytes<8+Read16(io.header,0)+3*sizeof(ContentInfo)+Read16(io.header,4)*16);
        std::set<std::uint32_t> candidates{0,2,42};bool gate{};
        Check(!Run(io,candidates,gate));Check(gate);Check(candidates==std::set<std::uint32_t>{42});Check(io.pages==1);}
    for(auto count:{std::uint16_t(0),std::uint16_t(64),std::uint16_t(65),std::uint16_t(65535)}){
        auto io=Fixture(count);std::set<std::uint32_t> candidates{0,64,65534,99999};bool gate{};
        Check(!Run(io,candidates,gate));Check(gate);Check(io.max_capacity<=64);
        Check(io.pages==(unsigned(count)+63)/64);Check(candidates.contains(99999));
        Check(candidates.contains(0)==(count==0));
        Check(candidates.contains(64)==(count<=64));
        Check(candidates.contains(65534)==(count<=65534));
    }
    // Invalid ranges must fail before the content-list IPC or any deletion gate.
    for(int mode=0;mode<6;++mode){auto io=Fixture();
        switch(mode){case 0:io.bytes=7;break;case 1:--io.bytes;break;case 2:io.header[0]=255;io.header[1]=255;break;
            case 3:io.header[2]=255;io.header[3]=255;break;case 4:io.actual_header=7;break;case 5:io.actual_header=9;break;}
        std::set<std::uint32_t> candidates{0,2,42};bool gate{};
        Check(Run(io,candidates,gate)==Invalid);Check(!gate);Check(!io.pages);Check(candidates==std::set<std::uint32_t>({0,2,42}));
    }
    // A short, negative or oversized returned page is not permission to skip
    // references. The failing page must not reach the visitor.
    for(int delta:{-4,-1,1,100}){auto io=Fixture();io.count_adjust=delta;
        std::set<std::uint32_t> candidates{0,2,42};bool gate{};
        Check(Run(io,candidates,gate)==Invalid);Check(!gate);Check(candidates==std::set<std::uint32_t>({0,2,42}));}
    // Changes after earlier pages were visited leave source deletion disallowed.
    for(bool header:{false,true}){auto io=Fixture(65);io.change_header=header;io.change_size=!header;
        std::set<std::uint32_t> candidates{0,64,42,99999};bool gate{};
        Check(Run(io,candidates,gate)==Invalid);Check(!gate);Check(candidates.contains(99999));}
    auto complete=Fixture(130);std::set<std::uint32_t> candidates{0,129,1000};bool gate{};
    Check(!Run(complete,candidates,gate));Check(gate);const auto count=complete.calls;
    for(unsigned at=1;at<=count;++at){auto io=Fixture(130);io.fail_at=at;
        std::set<std::uint32_t> remaining{0,129,1000};bool may_delete{};
        Check(Run(io,remaining,may_delete)==Fault);Check(!may_delete);Check(remaining.contains(1000));}
    std::cout<<"Installed reference scan: "<<assertions<<" checks, "<<count<<" injected cancellation/I/O failure points passed\n";
}
