// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace nexus::catalog {
using Bytes=std::vector<std::uint8_t>;
constexpr std::size_t MaxBytes=1024*1024,MaxRules=2048;
constexpr std::array<std::uint8_t,8> Magic{'N','X','R','C','A','T','1',0};
enum class Condition:std::uint8_t {MissingLinks=1,ConvertedMetadata,IncompleteFiles,MissingRegistration,MissingTicket,MissingMaps};
enum class Action:std::uint8_t {RepairLinks=1,VerifyFiles,ReinstallOriginal,ImportRegistration,RestoreBackup};
struct Rule {
    std::string id,title,advice;
    std::uint64_t application{};
    Condition condition{};Action action{};std::uint8_t type{};
    std::uint32_t version_min{},version_max{},manager_min{};
    std::array<std::uint8_t,16> content{};
    std::array<std::uint8_t,32> sha256{};
};
struct Catalog {std::uint64_t revision{};std::uint32_t manager_min{},manager_max{};std::vector<Rule> rules;};
struct Evidence {
    Condition condition{};std::uint64_t application{};std::uint32_t version{};std::uint8_t type{};
    std::array<std::uint8_t,16> content{};std::array<std::uint8_t,32> sha256{};
};
inline bool Allowed(Condition c,Action a){
    switch(c){
        case Condition::MissingLinks:return a==Action::RepairLinks;
        case Condition::ConvertedMetadata:return a==Action::VerifyFiles;
        case Condition::IncompleteFiles:case Condition::MissingTicket:return a==Action::ReinstallOriginal;
        case Condition::MissingRegistration:return a==Action::ImportRegistration;
        case Condition::MissingMaps:return a==Action::RestoreBackup;
    }return false;
}
inline bool Text(std::string_view s){
    // Bounded, well-formed UTF-8 only; no NUL, control or terminal escape text.
    for(std::size_t i=0;i<s.size();){
        auto c=static_cast<unsigned char>(s[i++]);if(c<32||c==127)return false;if(c<128)continue;
        unsigned n{},v{},min{};
        if(c>=0xc2&&c<=0xdf){n=1;v=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef){n=2;v=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){n=3;v=c&7;min=0x10000;}else return false;
        if(n>s.size()-i)return false;
        while(n--){c=static_cast<unsigned char>(s[i++]);if((c&0xc0)!=0x80)return false;v=v*64+(c&63);}
        if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return false;
    }return !s.empty();
}
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t offset{};
    bool Take(void* out,std::size_t n){if(n>bytes.size()-offset)return false;std::memcpy(out,bytes.data()+offset,n);offset+=n;return true;}
    template<class T>bool Number(T& out){out=0;if(sizeof(T)>bytes.size()-offset)return false;for(unsigned i=0;i<sizeof(T);++i)out|=T(bytes[offset++])<<(i*8);return true;}
    bool String(std::string& out,std::size_t max){std::uint16_t n{};if(!Number(n)||!n||n>max||n>bytes.size()-offset)return false;out.assign(reinterpret_cast<const char*>(bytes.data()+offset),n);offset+=n;return Text(out);}
};
// The verifier sees the complete envelope header and payload. No unsigned fields
// or downloaded code can supply paths, disable validation or modify game files.
template<class Verify>
bool Decode(std::span<const std::uint8_t> bytes,std::uint32_t manager,Catalog& out,Verify verify){
    out={};if(bytes.size()<16+24+256||bytes.size()>MaxBytes||!std::equal(Magic.begin(),Magic.end(),bytes.begin()))return false;
    Reader envelope{bytes.subspan(8,8)};std::uint32_t size{},sig{};
    if(!envelope.Number(size)||!envelope.Number(sig)||sig!=256||size!=bytes.size()-16-256)return false;
    if(!verify(bytes.first(16+size),bytes.last(256)))return false;
    Reader r{bytes.subspan(16,size)};Catalog value;std::uint32_t schema{},count{};
    if(!r.Number(schema)||schema!=1||!r.Number(value.revision)||!value.revision||
       !r.Number(value.manager_min)||!r.Number(value.manager_max)||!r.Number(count)||
       !count||count>MaxRules||!value.manager_min||value.manager_min>manager||value.manager_max<manager)return false;
    std::set<std::string> ids;
    for(std::uint32_t i=0;i<count;++i){
        Rule rule;std::uint8_t condition{},action{},reserved{};
        if(!r.String(rule.id,64)||!r.Number(rule.application)||!r.Number(condition)||!r.Number(action)||!r.Number(rule.type)||
           !r.Number(reserved)||reserved||!r.Number(rule.version_min)||!r.Number(rule.version_max)||!r.Number(rule.manager_min)||
           !r.Take(rule.content.data(),rule.content.size())||!r.Take(rule.sha256.data(),rule.sha256.size())||
           !r.String(rule.title,160)||!r.String(rule.advice,512))return false;
        rule.condition=Condition(condition);rule.action=Action(action);
        const bool content=rule.content!=std::array<std::uint8_t,16>{},hash=rule.sha256!=std::array<std::uint8_t,32>{};
        if(!Allowed(rule.condition,rule.action)||rule.version_min>rule.version_max||!rule.manager_min||
           (rule.type&& (rule.type<0x80||rule.type>0x83))||content!=hash||
           (content&&(!rule.application||rule.condition!=Condition::ConvertedMetadata))||
           !std::all_of(rule.id.begin(),rule.id.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';})||
           !ids.insert(rule.id).second)return false;
        value.rules.push_back(std::move(rule));
    }
    if(r.offset!=r.bytes.size())return false;out=std::move(value);return true;
}
inline std::vector<std::size_t> Match(const Catalog& c,const Evidence& e,std::uint32_t manager){
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<c.rules.size();++i){const auto&r=c.rules[i];
        if(r.condition!=e.condition||r.manager_min>manager||(r.application&&r.application!=e.application)||
           (r.type&&r.type!=e.type)||e.version<r.version_min||e.version>r.version_max)continue;
        if(r.content!=std::array<std::uint8_t,16>{}&&(r.content!=e.content||r.sha256!=e.sha256))continue;
        result.push_back(i);
    }return result;
}
// A signature is necessary but does not authorize rollback or same-revision
// replacement. The caller persists a new candidate only after this succeeds.
inline bool CanReplace(const Catalog& current,std::span<const std::uint8_t> old_bytes,const Catalog& next,std::span<const std::uint8_t> next_bytes){
    return next.revision>current.revision||(next.revision==current.revision&&std::equal(old_bytes.begin(),old_bytes.end(),next_bytes.begin(),next_bytes.end()));
}
}
