// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/index_repair_core.hpp"
#include <compare>
#include <tuple>
#include <optional>

namespace nexus::usbimport {
using repair::Image;
using recovery::Bytes;
inline constexpr char Pending[]="/atmosphere/usb-import.pending";
inline constexpr size_t MaxJournal=16*1024*1024, MaxMeta=1024*1024, MaxRecords=4096;
struct Key {
    std::uint64_t id{};std::uint32_t version{};std::uint8_t type{},install{};
    auto operator<=>(const Key&) const = default;
};
struct Meta {Key key;Image before,after;};
struct Journal {
    std::uint64_t nonce{},application{};
    std::array<std::uint8_t,32> system{};
    repair::Snapshot before,after;
    std::vector<Meta> metadata;
    // Canonical NS wire records (24 bytes each, zero padding), with the
    // original application event stored separately for accurate rollback.
    Image home_before,home_after;
    std::uint8_t home_event{3};
    std::array<Image,2> badges;
};
inline bool ValidSystem(const std::array<std::uint8_t,32>& id){return std::any_of(id.begin(),id.end(),[](auto b){return b!=0;});}
inline bool ValidKey(const Key& k){return k.id && !k.install && (k.type==0x80 || k.type==0x81 || k.type==0x82 || k.type==0x84);}
inline bool ValidHome(const Image& image){
    if(!image.exists)return image.data.empty();
    if(image.data.size()%24 || image.data.size()>MaxRecords*24)return false;
    // Exact canonical NS records; no wire padding, duplicate identities,
    // system metadata or unsupported storage IDs may reach rollback writes.
    using Identity=std::tuple<std::uint64_t,std::uint8_t,std::uint32_t,std::uint8_t,std::uint8_t>;
    std::optional<Identity> previous;
    for(size_t p=0;p<image.data.size();p+=24){
        const auto& b=image.data;Key key{repair::Get64(b,p),repair::Get32(b,p+8),b[p+12],b[p+13]};const auto storage=b[p+16];
        if(!ValidKey(key) || (storage!=2 && storage!=4 && storage!=5) || b[p+14] || b[p+15])return false;
        for(size_t i=17;i<24;++i)if(b[p+i])return false;
        const Identity current{key.id,key.type,key.version,key.install,storage};
        if(previous && *previous>=current)return false;
        previous=current;
    }
    return true;
}
inline std::string Stage(const Journal& j){char b[96];snprintf(b,sizeof(b),"/atmosphere/usb-import-%016llx",static_cast<unsigned long long>(j.nonce));return b;}
inline void PutImage(Bytes& b,const Image& f){recovery::U32(b,f.exists);recovery::U32(b,f.data.size());b.insert(b.end(),f.data.begin(),f.data.end());}
inline Bytes Encode(const Journal& j){
    Bytes b;recovery::U32(b,0x5049584e);recovery::U32(b,2);recovery::U64(b,j.nonce);recovery::U64(b,j.application);
    recovery::U32(b,j.home_event);recovery::U32(b,j.metadata.size());
    b.insert(b.end(),j.system.begin(),j.system.end());
    for(const auto* s:{&j.before,&j.after})for(const auto& f:*s)PutImage(b,f);
    PutImage(b,j.home_before);PutImage(b,j.home_after);
    for(const auto& f:j.badges)PutImage(b,f);
    for(const auto& m:j.metadata){recovery::U64(b,m.key.id);recovery::U32(b,m.key.version);recovery::U32(b,m.key.type|(std::uint32_t(m.key.install)<<8));PutImage(b,m.before);PutImage(b,m.after);}
    recovery::U32(b,repair::Crc(b,b.size()));return b;
}
inline bool Decode(const Bytes& b,Journal& j){
    if(b.size()<164 || b.size()>MaxJournal || repair::Get32(b,0)!=0x5049584e || repair::Get32(b,4)!=2 || repair::Get32(b,b.size()-4)!=repair::Crc(b,b.size()-4))return false;
    Journal out;out.nonce=repair::Get64(b,8);out.application=repair::Get64(b,16);
    const auto event=repair::Get32(b,24),count=repair::Get32(b,28);
    if(!out.nonce || !out.application || event>255 || !count || count>MaxRecords)return false;
    std::copy_n(b.begin()+32,out.system.size(),out.system.begin());if(!ValidSystem(out.system))return false;
    out.home_event=event;size_t p=64;
    auto image=[&](Image& f,size_t limit){
        if(p>b.size()-4 || b.size()-4-p<8)return false;
        const auto present=repair::Get32(b,p),size=repair::Get32(b,p+4);p+=8;
        if(present>1 || size>limit || size>b.size()-4-p || (!present && size))return false;
        f.exists=present;f.data.assign(b.begin()+p,b.begin()+p+size);p+=size;return true;
    };
    for(auto* s:{&out.before,&out.after})for(auto& f:*s)if(!image(f,repair::MaxFile))return false;
    repair::Indexes a,z;if(!repair::Decode(out.before,a)||!repair::Decode(out.after,z))return false;
    if(!image(out.home_before,MaxRecords*24)||!image(out.home_after,MaxRecords*24)||!out.home_after.exists || out.home_after.data.empty() || !ValidHome(out.home_before) || !ValidHome(out.home_after))return false;
    for(auto& f:out.badges)if(!image(f,1024*1024))return false;
    std::set<Key> keys;std::set<std::pair<std::uint64_t,std::uint8_t>> identities;
    for(size_t i=0;i<count;++i){
        if(p>b.size()-4 || b.size()-4-p<16)return false;
        Meta m;m.key.id=repair::Get64(b,p);m.key.version=repair::Get32(b,p+8);const auto kind=repair::Get32(b,p+12);p+=16;
        m.key.type=kind&255;m.key.install=(kind>>8)&255;
        if(!ValidKey(m.key) || kind>>16 || !keys.insert(m.key).second || !identities.emplace(m.key.id,m.key.type).second || !image(m.before,MaxMeta)||!image(m.after,MaxMeta)||!m.after.exists || m.after.data.size()<8 || (m.before.exists&&m.before.data.size()<8))return false;
        out.metadata.push_back(std::move(m));
    }
    if(p!=b.size()-4)return false;
    j=std::move(out);return true;
}
inline bool SameIndexes(const repair::Snapshot& a,const repair::Snapshot& b){repair::Indexes x,y;return repair::Decode(a,x)&&repair::Decode(b,y)&&x==y;}
// Only exact before/after values, or an absent file left by a journaled rename,
// may be rolled back. An unrelated writer's changes are never overwritten.
inline bool RestorableIndexes(const repair::Snapshot& now,const Journal& j){
    for(size_t i=0;i<3;++i)if(now[i].exists && now[i]!=j.before[i] && now[i]!=j.after[i]){
        repair::Snapshot n{},a{},b{};n[i]=now[i];a[i]=j.before[i];b[i]=j.after[i];
        if(!SameIndexes(n,a)&&!SameIndexes(n,b))return false;
    }
    return true;
}
template<class IO> bool CanRestore(IO& io,const Journal& j){
    if(!ValidSystem(j.system)||!io.SystemMatches(j))return false;
    repair::Snapshot now;if(!io.Indexes(now)||!RestorableIndexes(now,j))return false;
    for(const auto& m:j.metadata){Image f;if(!io.MetaRead(m.key,f)||(f!=m.before&&f!=m.after))return false;}
    Image home;return io.HomeRead(j.application,home) && (home==j.home_before || home==j.home_after || io.HomeIntermediate(home,j));
}
// Adapter handles close+readback+commit for every write. Rechecking all state
// precedes rollback. Repeated rollback is safe after interruption at any step.
template<class IO> bool Restore(IO& io,const Journal& j){
    if(!CanRestore(io,j))return false;
    if(!io.HomeWrite(j.application,j.home_before,j.home_event))return false;
    for(const auto& m:j.metadata)if(!io.MetaWrite(m.key,m.before))return false;
    if(!io.IndexesRestore(j))return false;
    return io.Finish(j,false);
}
}
