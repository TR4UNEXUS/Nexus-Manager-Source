// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "nexus/registration_protocol.hpp"
namespace nexus::recovery {
using Id = std::array<std::uint8_t,16>;
using Bytes = std::vector<std::uint8_t>;
inline bool Nonzero(const Id& id) { return id != Id{}; }
struct Content { Id id{}; std::uint64_t size{}; std::uint8_t type{}, offset{}; };
struct Title { std::uint64_t id{}; Id meta{}, control{}, program{}; };
struct Maps { std::vector<Title> titles; std::set<Id> content; };
inline bool Valid(const Content& c) { return Nonzero(c.id) && c.size>=0xc00 && c.type<=6; }
// One complete, physically checked NCM record. No title/alias is guessed from
// file names, and no partial result is added when a record fails validation.
inline bool Add(Maps& maps, std::uint64_t id, bool application, const std::vector<Content>& items) {
    if(!id || items.empty() || items.size()>4096)return false;
    auto ids=maps.content;
    std::set<Id> seen;
    Title title{id}; int control=256,program=256,metas=0;
    for(const auto& c:items){
        if(!Valid(c) || !seen.insert(c.id).second)return false;
        ids.insert(c.id);
        if(c.type==0){title.meta=c.id; ++metas;}
        if(c.type==3 && c.offset<control){control=c.offset;title.control=c.id;}
        if(c.type==1 && c.offset<program){program=c.offset;title.program=c.id;}
    }
    if(ids.size()>registration::MaxContent)return false;
    if(application){
        if(metas!=1 || !Nonzero(title.control) || !Nonzero(title.program) || maps.titles.size()>=registration::MaxTitles)return false;
        for(const auto& t:maps.titles)if(t.id==id)return false;
        maps.titles.push_back(title);
    }
    maps.content=std::move(ids);return true;
}
inline void U32(Bytes& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(n>>(i*8));}
inline void U64(Bytes& b,std::uint64_t n){for(int i=0;i<8;++i)b.push_back(n>>(i*8));}
inline void Put(Bytes& b,const Id& id){b.insert(b.end(),id.begin(),id.end());}
inline Bytes Header(std::uint32_t magic,std::uint32_t version,std::size_t count){
    Bytes b;U32(b,magic);U32(b,version);U32(b,count);U32(b,0);return b;
}
inline std::array<Bytes,3> Encode(const Maps& maps){
    std::array<Bytes,3> out{Header(0x54425355,1,maps.titles.size()),Header(0x43425355,2,maps.titles.size()),Header(0x44425355,1,maps.content.size())};
    for(const auto& t:maps.titles){
        U64(out[0],t.id);U64(out[0],0); // opaque launch aliases must be rediscovered
        U64(out[1],t.id);Put(out[1],t.meta);Put(out[1],t.control);Put(out[1],t.program);
    }
    for(const auto& id:maps.content)Put(out[2],id);
    return out;
}
inline constexpr const char* Names[]{"title_map.bin","title_content_map.bin","content_locations.bin"};
// IO owns a new adjacent staging directory. Nothing live is replaced/deleted.
// Each write includes close + readback verification; promotion is one directory
// rename. Failed/cancelled attempts retain staging files for inspection.
template<class IO> bool Publish(IO& io,const Maps& maps){
    if(maps.titles.empty() || maps.titles.size()>registration::MaxTitles || maps.content.size()>registration::MaxContent ||
       !io.Check() || !io.Missing() || !io.Begin())return false;
    const auto data=Encode(maps);
    for(int i=0;i<3;++i)if(!io.Check() || !io.Write(Names[i],data[i]))return false;
    return io.Check() && io.Missing() && io.Promote();
}
}
