// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/index_repair_core.hpp"
namespace nexus::routing_backup {
inline constexpr std::uint32_t Magic=0x3442584e;
inline constexpr std::size_t HeaderSize=176;
struct Status {std::uint32_t magic{},version{},flags{},last_result{};std::uint64_t sequence{},bytes{};};
static_assert(sizeof(Status)==32);
struct Diagnostic {std::uint32_t magic{},version{},stage{},result{},index{},reserved{};std::uint64_t expected{},actual{},attempts{};};
static_assert(sizeof(Diagnostic)==48);
inline bool Valid(const Diagnostic&d){return d.magic==0x3344584e&&d.version==1&&d.stage<=15&&!d.reserved&&(d.index==0xffffffff||d.index<3);}

inline bool Valid(const Status&s){return s.magic==Magic&&s.version==1&&!(s.flags&~1u)&&(!(s.flags&1)||(s.sequence&&s.bytes>=HeaderSize&&s.bytes<=HeaderSize+3*repair::MaxFile));}
template<class Hash> bool Decode(const recovery::Bytes&b,std::uint64_t expected,repair::Snapshot&out,Hash hash){
    if(b.size()<HeaderSize||repair::Get32(b,0)!=Magic||repair::Get32(b,4)!=1||repair::Get32(b,8)!=HeaderSize||repair::Get32(b,12)!=7||repair::Get64(b,16)!=expected||!expected)return false;
    const auto header=hash(b.data(),144);if(!std::equal(header.begin(),header.end(),b.begin()+144))return false;
    repair::Snapshot value;std::size_t offset=HeaderSize;
    for(unsigned i=0;i<3;++i){const auto size=repair::Get64(b,24+i*8);if(size<16||size>repair::MaxFile||size>b.size()-offset)return false;
        const auto digest=hash(b.data()+offset,size);if(!std::equal(digest.begin(),digest.end(),b.begin()+48+i*32))return false;
        value[i].exists=true;value[i].data.assign(b.begin()+offset,b.begin()+offset+size);offset+=size;
    }
    repair::Indexes parsed;if(offset!=b.size()||!repair::Decode(value,parsed))return false;
    // A base-game route needs its exact recorded Meta/Control/Program tuple.
    // A damaged tuple must not turn into a guess or an unrelated USB title.
    for(const auto&[id,alias]:parsed.titles){
        const auto it=parsed.mappings.find(id);if(it==parsed.mappings.end())return false;
        for(const auto&content:it->second)if(!recovery::Nonzero(content)||!parsed.content.contains(content))return false;
        if(alias==id)return false;
    }
    out=std::move(value);return true;
}
}
