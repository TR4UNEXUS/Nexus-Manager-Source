// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/index_recovery_core.hpp"
#include <map>
#include <cstdio>
namespace nexus::repair {
using namespace recovery;
struct Image { bool exists{}; Bytes data; bool operator==(const Image&) const = default; };
using Snapshot=std::array<Image,3>;
struct Indexes {
    std::map<std::uint64_t,std::uint64_t> titles;
    std::map<std::uint64_t,std::array<Id,3>> mappings;
    std::set<Id> content;
    bool operator==(const Indexes&) const = default;
};
inline constexpr size_t MaxFile=16+registration::MaxContent*16, MaxJournal=16*1024*1024;
inline std::uint32_t Get32(const Bytes& b,size_t p){
    std::uint32_t v=0;for(int i=0;i<4;++i)v|=std::uint32_t(b[p+i])<<(8*i);return v;
}
inline std::uint64_t Get64(const Bytes& b,size_t p){return Get32(b,p)|(std::uint64_t(Get32(b,p+4))<<32);}
inline Id GetId(const Bytes& b,size_t p){Id v{};std::copy_n(b.begin()+p,16,v.begin());return v;}
// Invalid/truncated/unknown indexes are never interpreted as empty indexes.
// Preserve nonzero aliases, unrelated titles and legacy v1 mappings.
inline bool Decode(const Snapshot& s,Indexes& result){
    Indexes out;
    constexpr std::uint32_t magic[]{0x54425355,0x43425355,0x44425355};
    for(size_t f=0;f<3;++f){
        const auto& b=s[f].data;
        if(!s[f].exists){if(!b.empty())return false;continue;}
        if(b.size()<16 || b.size()>MaxFile || Get32(b,0)!=magic[f] || Get32(b,12))return false;
        const auto ver=Get32(b,4),count=Get32(b,8);
        if((f==1 ? (ver!=1 && ver!=2) : ver!=1) || count>(f==2?registration::MaxContent:registration::MaxTitles))return false;
        const size_t stride=f==1?(ver==1?40:56):16;
        if(b.size()!=16+count*stride)return false;
        for(size_t i=0,p=16;i<count;++i,p+=stride){
            if(f==2){auto id=GetId(b,p);if(!Nonzero(id) || !out.content.insert(id).second)return false;}
            else {
                auto id=Get64(b,p);if(!id)return false;
                if(f==0){if(!out.titles.emplace(id,Get64(b,p+8)).second)return false;}
                else {
                    std::array<Id,3> ids{};
                    if(ver==2)ids[0]=GetId(b,p+8);
                    ids[1]=GetId(b,p+(ver==2?24:8));ids[2]=GetId(b,p+(ver==2?40:24));
                    if(!out.mappings.emplace(id,ids).second)return false;
                }
            }
        }
    }
    result=std::move(out);return true;
}
inline Snapshot Encode(const Indexes& m){
    Snapshot s{{{true,Header(0x54425355,1,m.titles.size())},{true,Header(0x43425355,2,m.mappings.size())},{true,Header(0x44425355,1,m.content.size())}}};
    for(const auto& [id,alias]:m.titles){U64(s[0].data,id);U64(s[0].data,alias);}
    for(const auto& [id,ids]:m.mappings){U64(s[1].data,id);for(const auto& v:ids)Put(s[1].data,v);}
    for(const auto& id:m.content)Put(s[2].data,id);
    return s;
}
enum class MergeResult { Invalid, Conflict, Limit, Intact, Changed };
inline MergeResult Merge(Indexes& m,std::uint64_t id,bool application,const std::vector<Content>& content){
    Maps candidate;if(!Add(candidate,id,application,content))return MergeResult::Invalid;
    auto next=m;
    if(application){
        const auto& t=candidate.titles[0];const std::array<Id,3> ids{t.meta,t.control,t.program};
        auto [it,added]=next.mappings.emplace(id,ids);
        if(!added)for(size_t i=0;i<3;++i){
            if(Nonzero(it->second[i]) && it->second[i]!=ids[i])return MergeResult::Conflict;
            it->second[i]=ids[i];
        }
        next.titles.emplace(id,0); // emplace deliberately preserves existing alias
    }
    next.content.insert(candidate.content.begin(),candidate.content.end());
    if(next.titles.size()>registration::MaxTitles || next.mappings.size()>registration::MaxTitles || next.content.size()>registration::MaxContent)return MergeResult::Limit;
    if(next==m)return MergeResult::Intact;
    m=std::move(next);return MergeResult::Changed;
}
// Keep bytes (including order and legacy format) of semantically unchanged files.
inline Snapshot Output(const Snapshot& before,const Indexes& original,const Indexes& merged){
    auto out=Encode(merged);
    if(original.titles==merged.titles)out[0]=before[0];
    if(original.mappings==merged.mappings)out[1]=before[1];
    if(original.content==merged.content)out[2]=before[2];
    return out;
}
struct Journal { std::uint64_t nonce{};Snapshot before,after; };
inline std::uint32_t Crc(const Bytes& b,size_t n){
    std::uint32_t c=~0u;for(size_t i=0;i<n;++i){c^=b[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;
}
inline Bytes EncodeJournal(const Journal& j){
    Bytes b;U32(b,0x5258554e);U32(b,1);U64(b,j.nonce);
    for(const auto* s:{&j.before,&j.after})for(const auto& f:*s){U32(b,f.exists?1:0);U32(b,f.data.size());b.insert(b.end(),f.data.begin(),f.data.end());}
    U32(b,Crc(b,b.size()));return b;
}
inline bool DecodeJournal(const Bytes& b,Journal& j){
    if(b.size()<68 || b.size()>MaxJournal || Get32(b,0)!=0x5258554e || Get32(b,4)!=1 || Get32(b,b.size()-4)!=Crc(b,b.size()-4))return false;
    Journal out;out.nonce=Get64(b,8);size_t p=16;
    for(auto* s:{&out.before,&out.after})for(auto& f:*s){
        if(p+8>b.size()-4)return false;
        auto present=Get32(b,p),size=Get32(b,p+4);p+=8;
        if(present>1 || size>MaxFile || size>b.size()-4-p || (!present && size))return false;
        f.exists=present;f.data.assign(b.begin()+p,b.begin()+p+size);p+=size;
    }
    Indexes a,z;if(p!=b.size()-4 || !Decode(out.before,a) || !Decode(out.after,z) || out.before==out.after)return false;
    j=std::move(out);return true;
}
inline constexpr const char* Folder="/atmosphere/usb_content";
inline constexpr const char* Pending="/atmosphere/usb_content-repair.pending";
inline std::string Stage(const Journal& j){char b[96];snprintf(b,sizeof(b),"/atmosphere/usb_content-repair-%016llx",static_cast<unsigned long long>(j.nonce));return b;}
inline std::string Live(size_t i){return std::string(Folder)+"/"+Names[i];}
template<class IO> bool Match(IO& io,const Snapshot& s){
    for(size_t i=0;i<3;++i){Image f;if(!io.Read(Live(i),f) || f!=s[i])return false;}return true;
}
// Journal + verified originals are committed BEFORE any live file is moved.
// Only these three exact names are touched. No directory enumeration/deletion.
// There is no filesystem-wide atomic rename: a pending journal explicitly
// requires rollback and reboot after an interrupted multi-file publication.
template<class IO> bool Publish(IO& io,const Journal& j){
    Image pending;
    if(!io.Check() || !io.Read(Pending,pending) || pending.exists || !Match(io,j.before))return false;
    const auto stage=Stage(j),journal=stage+"/journal.bin";
    if(!io.Begin(stage))return false;
    for(size_t i=0;i<3;++i){
        if(!io.Check())return false;
        if(j.before[i].exists && !io.WriteNew(stage+"/original-"+Names[i],j.before[i].data))return false;
        if(j.after[i]!=j.before[i] && j.after[i].exists && !io.WriteNew(stage+"/new-"+Names[i],j.after[i].data))return false;
    }
    if(!io.WriteNew(journal,EncodeJournal(j)) || !io.Commit() || !io.Check() || !Match(io,j.before) || !io.Read(Pending,pending) || pending.exists)return false;
    // A conservative exit requirement applies even if journal activation fails.
    io.Activated();
    if(!io.Rename(journal,Pending) || !io.Commit())return false;
    for(size_t i:{2u,1u,0u}){
        if(j.after[i]==j.before[i])continue;
        Image now;if(!io.Check() || !io.Read(Live(i),now) || now!=j.before[i])return false;
        if(now.exists && !io.Rename(Live(i),stage+"/moved-"+Names[i]))return false;
        if(j.after[i].exists && !io.Rename(stage+"/new-"+Names[i],Live(i)))return false;
        if(!io.Commit())return false;
    }
    return Match(io,j.after) && io.Rename(Pending,stage+"/completed.bin") && io.Commit();
}
// Roll back, never guess how to complete an interrupted install. Check all live
// files first; if another program changed one, preserve everything for diagnosis.
template<class IO> bool Restore(IO& io,const Journal& j){
    Image pending;if(!io.Check() || !io.Read(Pending,pending) || pending!=Image{true,EncodeJournal(j)})return false;
    for(size_t i=0;i<3;++i){Image now;if(!io.Read(Live(i),now) || (now.exists && now!=j.before[i] && now!=j.after[i]))return false;}
    const auto stage=Stage(j);
    io.Activated();
    for(size_t i:{0u,1u,2u}){
        Image now;if(!io.Check() || !io.Read(Live(i),now))return false;
        if(now==j.before[i])continue;
        if(now.exists && now!=j.after[i])return false;
        const auto restore=stage+"/restore-"+Names[i];
        if(j.before[i].exists){
            Image staged;if(!io.Read(restore,staged))return false;
            if(staged.exists ? staged!=j.before[i] : !io.WriteNew(restore,j.before[i].data))return false;
            if(!io.Commit())return false;
        }
        if(now.exists && !io.Rename(Live(i),stage+"/replaced-"+Names[i]))return false;
        if(j.before[i].exists && !io.Rename(restore,Live(i)))return false;
        if(!io.Commit())return false;
    }
    return Match(io,j.before) && io.Rename(Pending,stage+"/rolled-back.bin") && io.Commit();
}
}
