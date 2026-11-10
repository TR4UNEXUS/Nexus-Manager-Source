// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bounded, platform-independent decoding of the metadata used by USB import.
// This does not infer game titles, ownership or completeness from filenames.
namespace nexus::usb_import {
inline constexpr std::size_t MaxMetaBytes=4*1024*1024;
inline constexpr std::size_t MaxContent=4095;
inline bool Range(std::uint64_t offset,std::uint64_t size,std::uint64_t end){return offset<=end && size<=end-offset;}
template<class T> T Read(std::span<const std::uint8_t> bytes,std::size_t at){
    T result{};if(Range(at,sizeof(result),bytes.size()))std::memcpy(&result,bytes.data()+at,sizeof(result));return result;
}
inline int Hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline bool ContentName(std::string_view name,std::array<std::uint8_t,16>& id){
    if(name.size()!=36 || name.substr(32)!=".nca")return false;
    for(std::size_t i=0;i<id.size();++i){const auto a=Hex(name[i*2]),b=Hex(name[i*2+1]);if(a<0||b<0)return false;id[i]=a*16+b;}
    return std::any_of(id.begin(),id.end(),[](auto b){return b!=0;});
}
inline bool UserId(std::uint64_t id){return (id>>56)==1;}
inline bool SupportedType(std::uint8_t type){return type==0x80||type==0x81||type==0x82||type==0x84;}
inline bool Utf8(std::string_view text){
    for(std::size_t at=0;at<text.size();){
        const auto first=static_cast<std::uint8_t>(text[at++]);if(first<0x80)continue;
        unsigned count{};std::uint32_t value{},minimum{};
        if(first>=0xC2&&first<=0xDF){count=1;value=first&31;minimum=0x80;}
        else if(first>=0xE0&&first<=0xEF){count=2;value=first&15;minimum=0x800;}
        else if(first>=0xF0&&first<=0xF4){count=3;value=first&7;minimum=0x10000;}
        else return false;
        if(count>text.size()-at)return false;
        while(count--){const auto next=static_cast<std::uint8_t>(text[at++]);if((next&0xC0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum||value>0x10FFFF||(value>=0xD800&&value<=0xDFFF))return false;
    }
    return true;
}
struct Content {
    std::array<std::uint8_t,32> hash{};
    std::array<std::uint8_t,16> id{};
    std::uint64_t size{};
    std::uint8_t attributes{},type{},id_offset{};
};
struct Meta {
    std::uint64_t id{},application_id{};
    std::uint32_t version{};
    std::uint8_t type{};
    std::array<std::uint8_t,8> header{};
    std::vector<std::uint8_t> extended,tail;
    std::vector<Content> content;
};
inline bool ParseCnmt(std::span<const std::uint8_t> bytes,Meta& out,std::string& reason){
    out={};reason.clear();
    const auto fail=[&](const char* text){reason=text;out={};return false;};
    if(bytes.size()<32||bytes.size()>MaxMetaBytes)return fail("Content metadata is truncated or too large.");
    out.id=Read<std::uint64_t>(bytes,0);out.version=Read<std::uint32_t>(bytes,8);out.type=bytes[12];
    if(!SupportedType(out.type)||!UserId(out.id))return fail("This is not supported game, update or DLC metadata.");
    if(bytes[13]!=0||bytes[22]!=0)return fail("This metadata is not a complete Switch content installation.");
    const auto ext=Read<std::uint16_t>(bytes,14),count=Read<std::uint16_t>(bytes,16),meta_count=Read<std::uint16_t>(bytes,18);
    const std::size_t minimum=out.type==0x81?24:out.type==0x84?32:16;
    if(ext<minimum||ext>4096||!Range(32,ext,bytes.size())||!count||count>MaxContent||meta_count>4096)
        return fail("Content metadata has invalid counts or an incomplete extended header.");
    const std::uint64_t content_offset=32+ext,tail_offset=content_offset+std::uint64_t(count)*56;
    if(!Range(content_offset,std::uint64_t(count)*56,bytes.size()))return fail("Content metadata is missing its content list.");
    std::uint64_t tail_size=std::uint64_t(meta_count)*16;
    if(out.type==0x81)tail_size+=Read<std::uint32_t>(bytes,32+12);
    if(out.type==0x84)tail_size+=Read<std::uint32_t>(bytes,32+20);
    if(!Range(tail_offset,tail_size,bytes.size()))return fail("Content metadata is missing its extended data.");
    const auto remainder=bytes.size()-tail_offset-tail_size;
    if(remainder!=0 && remainder!=32)return fail("Content metadata has an unexpected trailing layout.");
    out.application_id=out.type==0x80?out.id:Read<std::uint64_t>(bytes,out.type==0x84?40:32);
    if(!UserId(out.application_id))return fail("Content metadata has no valid parent game.");
    std::copy_n(bytes.begin()+14,8,out.header.begin());
    out.extended.assign(bytes.begin()+32,bytes.begin()+content_offset);
    out.tail.assign(bytes.begin()+tail_offset,bytes.begin()+tail_offset+tail_size);
    std::set<std::array<std::uint8_t,16>> unique;
    for(std::size_t i=0;i<count;++i){
        const auto at=content_offset+i*56;Content c{};
        std::copy_n(bytes.begin()+at,32,c.hash.begin());std::copy_n(bytes.begin()+at+32,16,c.id.begin());
        c.size=Read<std::uint32_t>(bytes,at+48)|(std::uint64_t(bytes[at+52])<<32);
        c.attributes=bytes[at+53];c.type=bytes[at+54];c.id_offset=bytes[at+55];
        if(c.size<0xC00||!std::any_of(c.id.begin(),c.id.end(),[](auto b){return b!=0;})||c.type<1||c.type>6||
            !std::equal(c.id.begin(),c.id.end(),c.hash.begin())||!unique.insert(c.id).second)
            return fail("Content metadata has an invalid or duplicate NCA reference.");
        out.content.push_back(c);
    }
    return true;
}

// Metadata NCAs have one CNMT in a PFS0. Parse every entry before accepting it;
// ambiguous names, non-root paths and truncated/overlapping ranges fail closed.
inline bool FindCnmt(std::span<const std::uint8_t> table,std::uint64_t total,std::uint64_t& offset,std::uint64_t& size){
    offset=size=0;if(table.size()<16||std::memcmp(table.data(),"PFS0",4))return false;
    const auto count=Read<std::uint32_t>(table,4),strings=Read<std::uint32_t>(table,8);
    const std::uint64_t names=16+std::uint64_t(count)*24,base=names+strings;
    if(!count||count>1024||base>table.size()||base>total)return false;
    bool found{};std::set<std::string_view> unique;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    for(std::uint32_t i=0;i<count;++i){
        const auto at=16+std::uint64_t(i)*24;const auto name=Read<std::uint32_t>(table,at+16);
        if(name>=strings)return false;
        const char* first=reinterpret_cast<const char*>(table.data()+names+name);
        const char* last=static_cast<const char*>(std::memchr(first,0,strings-name));
        if(!last)return false;
        const std::string_view text(first,last-first);
        if(text.empty()||text.find_first_of("/\\:")!=text.npos||!unique.insert(text).second)return false;
        const auto relative=Read<std::uint64_t>(table,at),length=Read<std::uint64_t>(table,at+8);
        if(!Range(relative,length,total-base))return false;
        if(length)ranges.emplace_back(relative,relative+length);
        if(text.ends_with(".cnmt")){
            if(found||length<32||length>MaxMetaBytes)return false;
            found=true;offset=base+relative;size=length;
        }
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].second)return false;
    return found;
}

inline bool FindRomfsRootFile(std::span<const std::uint8_t> table,std::uint64_t data_size,std::string_view wanted,std::uint64_t& offset,std::uint64_t& size){
    offset=size=0;bool found{};
    for(std::uint64_t at=0;at<table.size();){
        if(!Range(at,32,table.size()))return false;
        const auto name_size=Read<std::uint32_t>(table,at+28);
        if(!Range(at+32,name_size,table.size()))return false;
        const auto begin=Read<std::uint64_t>(table,at+8),length=Read<std::uint64_t>(table,at+16);
        if(!Range(begin,length,data_size))return false;
        if(Read<std::uint32_t>(table,at)==0&&std::string_view(reinterpret_cast<const char*>(table.data()+at+32),name_size)==wanted){
            if(found)return false;
            found=true;offset=begin;size=length;
        }
        const auto next=at+32+name_size,padding=(4-(next&3))&3;
        if(!Range(next,padding,table.size()))return false;
        at=next+padding;
    }
    return found;
}
}
