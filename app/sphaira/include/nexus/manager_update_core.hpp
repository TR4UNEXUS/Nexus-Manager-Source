// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/index_repair_core.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace nexus::manager_update {
inline constexpr std::uint64_t TitleId=0x05004e584d470000ULL;
inline constexpr std::size_t MaxMetadata=1024*1024;
using Bytes=std::vector<std::uint8_t>;
struct Identity {
    std::uint64_t title{};
    std::uint32_t version{};
    std::uint8_t type{},install_type{},storage{};
    bool operator==(const Identity&)const=default;
};
// Stable NCM wire values. The Switch adapter asserts them against libnx.
inline bool AllowedStorage(unsigned storage){return storage==4 || storage==5;}
inline bool SameInstalledApplication(const Identity& installed,const Identity& incoming,std::size_t records){
    return records==1 && installed==incoming && installed.title==TitleId &&
        installed.type==0x80 && installed.install_type==0 && AllowedStorage(installed.storage);
}
enum class Kind{Meta,Program,Control,Other};
struct Content {
    std::array<std::uint8_t,16> id{};
    std::uint64_t owner{},size{};
    Kind kind{Kind::Other};
    unsigned offset{};
    bool rights_free{};
};
inline bool ValidLayout(std::span<const Content> content){
    if(content.size()!=3)return false;
    std::array<unsigned,3> counts{};
    for(std::size_t i=0;i<content.size();++i){const auto& c=content[i];
        if(c.owner!=TitleId || c.size<0xc00 || c.size>64ULL*1024*1024 || c.offset || !c.rights_free || static_cast<unsigned>(c.kind)>=counts.size())return false;
        ++counts[static_cast<unsigned>(c.kind)];
        bool nonzero=false;for(auto byte:c.id)nonzero|=byte!=0;if(!nonzero)return false;
        for(std::size_t j=0;j<i;++j)if(content[j].id==c.id)return false;
    }
    return counts==std::array<unsigned,3>{1,1,1};
}
inline bool DisplayVersionMatches(std::span<const char> display,std::string_view expected){
    if(display.size()!=16 || expected.empty() || expected.size()>=16 || expected.find('\0')!=expected.npos)return false;
    return std::string_view(display.data(),expected.size())==expected && display[expected.size()]=='\0';
}
inline bool DigestMatches(std::span<const std::uint8_t> digest,std::string_view expected){
    if(digest.size()!=32 || expected.size()!=64)return false;
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<digest.size();++i)
        if(expected[i*2]!=hex[digest[i]>>4] || expected[i*2+1]!=hex[digest[i]&15])return false;
    return true;
}
enum class Routing{Native,Usb,Invalid};
inline Routing CheckRouting(const nexus::repair::Snapshot& snapshot,std::span<const Content> installed){
    nexus::repair::Indexes indexes;if(!nexus::repair::Decode(snapshot,indexes))return Routing::Invalid;
    if(indexes.titles.contains(TitleId) || indexes.mappings.contains(TitleId))return Routing::Usb;
    for(const auto& content:installed){
        if(indexes.content.contains(content.id))return Routing::Usb;
        for(const auto& [title,ids]:indexes.mappings){
            (void)title;for(const auto& id:ids)if(id==content.id)return Routing::Usb;
        }
    }
    return Routing::Native;
}
enum class Publication{Installed,Unchanged,BackupFailed,Restored,RestoreFailed};
// New NCAs have already been fully published and verified. This transaction
// changes exactly one existing NCM value; NS, AVM, saves and old NCAs are outside
// its mutation surface. Cancellation is allowed only before the first Set.
template<class IO> Publication ReplaceMetadata(IO& io,const Bytes& before,const Bytes& after){
    if(before.empty() || after.empty() || before.size()>MaxMetadata || after.size()>MaxMetadata)return Publication::Unchanged;
    Bytes current;
    if(!io.Read(current) || current!=before || !io.RecordUnchanged() || !io.BeforeWrite())return Publication::Unchanged;
    if(!io.Backup(before,after))return Publication::BackupFailed;
    if(!io.Read(current) || current!=before || !io.RecordUnchanged() || !io.BeforeWrite())return Publication::Unchanged;
    if(io.Set(after) && io.Commit() && io.Read(current) && current==after && io.RecordUnchanged())return Publication::Installed;
    // Do not overwrite an unrelated concurrent mutation. A failed Set/Commit
    // may have made either whole value visible; both are owned states.
    if(!io.Read(current) || (current!=before && current!=after))return Publication::RestoreFailed;
    if(!io.Set(before) || !io.Commit() || !io.Read(current) || current!=before)return Publication::RestoreFailed;
    return Publication::Restored;
}
}
