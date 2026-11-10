// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace nexus::verification {
using Digest=std::array<std::uint8_t,32>;
struct Key {
    std::array<std::uint8_t,16> content{};
    Digest expected{};
    std::uint64_t generation{},size{};
    unsigned expected_bytes{};
    bool plaintext_alias{};
    bool operator==(const Key&)const=default;
    bool Valid()const{return generation&&size>=0xc00&&size<=INT64_MAX&&(expected_bytes==16||expected_bytes==32);}
};
// Only the active installer owns this state. Nothing is persisted in receipts
// or shared with another package, import, repair or removable-media generation.
struct Written {
    Key key{};Digest header{};
    bool header_known{},source_matched{},complete{},flushed{};
    bool Matches(const Key& current)const{
        return current.Valid()&&key==current&&header_known&&source_matched&&complete&&flushed;
    }
};
class Cache {
    struct Entry {Key key;Digest header;};
    std::vector<Entry> entries;
public:
    const Digest* Find(const Key& key)const{
        if(!key.Valid())return nullptr;
        for(const auto& entry:entries)if(entry.key==key)return &entry.header;
        return nullptr;
    }
    void Forget(const std::array<std::uint8_t,16>& content){
        std::erase_if(entries,[&](const auto& entry){return entry.key.content==content;});
    }
    void Remember(const Key& key,const Digest& header){
        Forget(key.content);
        // Bounded cache exhaustion simply restores full verification.
        if(key.Valid()&&entries.size()<4096)entries.push_back({key,header});
    }
};
}
