// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_import_scan_core.hpp"
#include <map>
#include <optional>
namespace nexus::pc {
using Id=std::array<std::uint8_t,16>;
struct Member {std::uint64_t title{};std::uint32_t version{};std::uint8_t type{};Id meta{};std::set<Id> ids;};
struct Receipt {std::uint64_t game{};std::vector<Member> members;};
inline bool Parse(std::span<const std::uint8_t> bytes,Receipt& out){
    using namespace nexus::usb_import;
    out={};if(bytes.size()<16||bytes.size()>4*1024*1024||std::memcmp(bytes.data(),"NPD1",4))return false;
    Receipt tmp;tmp.game=Read<std::uint64_t>(bytes,4);const auto count=Read<std::uint32_t>(bytes,12);
    if(!UserId(tmp.game)||!count||count>4096)return false;
    size_t pos=16;std::set<Id> metas;
    for(std::uint32_t i=0;i<count;++i){
        if(!Range(pos,33,bytes.size()))return false;
        Member m;m.title=Read<std::uint64_t>(bytes,pos);m.version=Read<std::uint32_t>(bytes,pos+8);m.type=bytes[pos+12];
        if(!UserId(m.title)||!SupportedType(m.type)||(m.type==0x80&&m.title!=tmp.game))return false;
        std::memcpy(m.meta.data(),bytes.data()+pos+13,16);if(m.meta==Id{}||!metas.insert(m.meta).second)return false;
        m.ids.insert(m.meta);const auto n=Read<std::uint32_t>(bytes,pos+29);pos+=33;
        if(!n||n>4095||!Range(pos,16ull*n,bytes.size()))return false;
        for(std::uint32_t j=0;j<n;++j,pos+=16){Id id;std::memcpy(id.data(),bytes.data()+pos,16);if(id==Id{}||!m.ids.insert(id).second)return false;}
        tmp.members.push_back(std::move(m));
    }
    if(pos!=bytes.size())return false;
    out=std::move(tmp);return true;
}
// NXS1 contains the exact old and new NPD1 receipts for a single application.
template<class Hash>
inline bool ParseReplacement(std::span<const std::uint8_t> payload,Receipt& before,Receipt& after,Hash hash){
    if(payload.size()<104||std::memcmp(payload.data(),"NXS1",4))return false;
    const auto original=nexus::usb_import::Read<std::uint32_t>(payload,4);
    if(original<48||original>4*1024*1024||original>payload.size()-8)return false;
    auto read=[&](auto framed,Receipt& r){
        if(framed.size()<48||framed.size()>4*1024*1024)return false;
        const auto body=framed.first(framed.size()-32);const auto checksum=hash(body);
        return std::equal(checksum.begin(),checksum.end(),framed.end()-32)&&Parse(body,r);
    };
    Receipt old,fresh;
    if(!read(payload.subspan(8,original),old)||!read(payload.subspan(8+original),fresh)||old.game!=fresh.game)return false;
    std::map<std::uint64_t,std::uint32_t> expected,actual;
    bool base=false;std::optional<std::uint32_t> update;
    for(const auto& m:old.members){
        if(m.type==0x81){if(update||m.title!=(old.game|0x800ull))return false;update=m.version;continue;}
        if(m.type==0x80)base=true;else if(m.type!=0x82)return false;
        if(!expected.emplace(m.title,m.version).second)return false;
    }
    if(!base)return false;
    if(update)expected[old.game]=*update;
    for(const auto& m:fresh.members){
        if((m.title==fresh.game?m.type!=0x80:m.type!=0x82)||!actual.emplace(m.title,m.version).second)return false;
    }
    if(expected!=actual)return false;
    before=std::move(old);after=std::move(fresh);return true;
}
inline bool Matches(const Receipt& r,std::uint64_t title,std::uint32_t version,std::uint8_t type,const std::set<Id>& ids){
    return std::any_of(r.members.begin(),r.members.end(),[&](const auto& m){return m.title==title&&m.version==version&&m.type==type&&m.ids==ids;});
}
inline bool TicketCandidates(std::span<const std::uint8_t> bytes,const std::array<std::uint8_t,32>& receipt,
        const std::array<std::uint8_t,32>& digest,const std::set<std::uint64_t>& local_titles,std::set<Id>& result){
    result.clear();
    if(bytes.size()<72||std::memcmp(bytes.data(),"NPR1",4)||
       !std::equal(receipt.begin(),receipt.end(),bytes.begin()+4)||
       !std::equal(digest.begin(),digest.end(),bytes.end()-32))return false;
    const auto count=nexus::usb_import::Read<std::uint32_t>(bytes,36);
    if(count>4096||bytes.size()!=72+16ull*count)return false;
    for(std::uint32_t i=0;i<count;++i){
        Id id{};std::memcpy(id.data(),bytes.data()+40+16*i,16);
        std::uint64_t title{};for(size_t j=0;j<8;++j)title=(title<<8)|id[j];
        if(local_titles.contains(title))result.insert(id);
    }
    return true;
}
}
