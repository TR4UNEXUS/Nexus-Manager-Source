// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/vsd_policy.hpp"
#include <cstdint>
#include <cstring>
#include <span>
#include <algorithm>
#include <string>
namespace nexus::library {
inline bool Range(std::uint64_t off,std::uint64_t size,std::uint64_t end){return off<=end && size<=end-off;}
template<class T> T Read(std::span<const std::uint8_t> b,size_t off){T v{};if(Range(off,sizeof(v),b.size()))std::memcpy(&v,b.data()+off,sizeof(v));return v;}
inline bool PfsFile(std::span<const std::uint8_t> b,std::uint64_t total,std::string_view wanted,std::uint64_t& offset,std::uint64_t& size){
    if(b.size()<16||std::memcmp(b.data(),"PFS0",4))return false;
    const auto count=Read<std::uint32_t>(b,4),strings=Read<std::uint32_t>(b,8);
    const std::uint64_t table=16+std::uint64_t(count)*24,base=table+strings;
    if(count>1024||base>b.size()||base>total)return false;
    bool found=false;
    for(std::uint32_t i=0;i<count;++i){
        const auto at=16+i*24;const auto name=Read<std::uint32_t>(b,at+16);
        if(name>=strings)return false;
        const auto* begin=reinterpret_cast<const char*>(b.data()+table+name);
        const auto* end=static_cast<const char*>(std::memchr(begin,0,strings-name));if(!end)return false;
        if(std::string_view(begin,end-begin)!=wanted)continue;
        if(found)return false;
        found=true;
        const auto relative=Read<std::uint64_t>(b,at);size=Read<std::uint64_t>(b,at+8);
        if(!Range(relative,size,total-base))return false;
        offset=base+relative;
    }
    return found;
}
// Extract bounded root files for migration. A malformed or duplicate entry is
// not a valid launch contract, even when its first path looks familiar.
inline bool RootText(std::span<const std::uint8_t> b,std::string_view wanted,std::string& out,bool& found){
    out.clear();found=false;if(b.size()<80 || Read<std::uint64_t>(b,0)!=80)return false;
    const auto table=Read<std::uint64_t>(b,56),len=Read<std::uint64_t>(b,64),data=Read<std::uint64_t>(b,72);
    if(!Range(table,len,b.size()) || data>b.size())return false;
    for(std::uint64_t off=0;off<len;){
        if(!Range(off,32,len))return false;
        const auto at=table+off;const auto n=Read<std::uint32_t>(b,at+28);
        if(!Range(off+32,n,len))return false;
        if(Read<std::uint32_t>(b,at)==0 && std::string_view(reinterpret_cast<const char*>(b.data()+at+32),n)==wanted){
            const auto start=Read<std::uint64_t>(b,at+8),size=Read<std::uint64_t>(b,at+16);
            if(found || size>=4096 || !Range(start,size,b.size()-data))return false;
            found=true;out.assign(reinterpret_cast<const char*>(b.data()+data+start),size);
            if(!out.empty()&&out.back()==0)out.pop_back();
            if(out.find('\0')!=out.npos)return false;
        }
        off+=(32+std::uint64_t(n)+3)&~std::uint64_t{3};
        if(off>len)return false;
    }
    return true;
}
inline bool ForwarderArguments(std::span<const std::uint8_t> b,const std::string& nro,std::string& tail){
    std::string args;bool found{};tail.clear();
    if(!RootText(b,"nextArgv",args,found))return false;
    if(!found || args.empty())return true;
    std::string first;size_t at=0;
    if(args[0]=='"'){const auto end=args.find('"',1);if(end==args.npos)return false;first=args.substr(1,end-1);at=end+1;}
    else {at=args.find(' ');if(at==args.npos)at=args.size();first=args.substr(0,at);}
    auto strip=[](std::string_view p){if(p.starts_with("sdmc:"))p.remove_prefix(5);return p;};
    if(strip(first)!=strip(nro) || (at<args.size()&&args[at]!=' '))return false;
    while(at<args.size()&&args[at]==' ')++at;
    tail=args.substr(at);
    return std::none_of(tail.begin(),tail.end(),[](unsigned char c){return c<32||c==127;});
}
struct Assets {std::uint64_t code{},icon{},icon_size{},nacp{},nacp_size{};};
inline bool Header(std::span<const std::uint8_t> h,std::uint64_t size,Assets& a){
    a={};if(h.size()<0x80 || std::memcmp(h.data()+0x10,"NRO0",4))return false;
    a.code=Read<std::uint32_t>(h,0x18);
    return a.code>=0x80 && a.code<=256*1024*1024 && a.code<=size;
}
inline bool AssetHeader(std::span<const std::uint8_t> h,std::uint64_t size,Assets& a){
    if(h.size()<56 || std::memcmp(h.data(),"ASET",4) || Read<std::uint32_t>(h,4)!=0 || a.code>size)return false;
    auto get=[&](size_t off,std::uint64_t limit,std::uint64_t& pos,std::uint64_t& len){
        auto x=Read<std::uint64_t>(h,off),n=Read<std::uint64_t>(h,off+8);
        if(!n){pos=len=0;return true;}
        if(x<56 || n>limit || !Range(x,n,size-a.code))return false;
        pos=a.code+x;len=n;return true;
    };
    return get(8,1024*1024,a.icon,a.icon_size) && get(24,0x4000,a.nacp,a.nacp_size) && (!a.nacp_size || a.nacp_size==0x4000);
}
// Recognize an actual root RomFS loader target, never guess from title ID/size.
inline std::string ForwarderPath(std::span<const std::uint8_t> b){
    if(b.size()<80 || Read<std::uint64_t>(b,0)!=80)return {};
    auto table=Read<std::uint64_t>(b,56),len=Read<std::uint64_t>(b,64),data=Read<std::uint64_t>(b,72);
    if(!Range(table,len,b.size()) || data>b.size())return {};
    for(std::uint64_t off=0;Range(off,32,len);){
        const auto at=table+off;auto name=Read<std::uint32_t>(b,at+28);
        if(!Range(off+32,name,len))return {};
        if(Read<std::uint32_t>(b,at)==0 && name==11 && !std::memcmp(b.data()+at+32,"nextNroPath",11)){
            auto start=Read<std::uint64_t>(b,at+8),size=Read<std::uint64_t>(b,at+16);
            if(!size || size>=512 || !Range(start,size,b.size()-data))return {};
            std::string path(reinterpret_cast<const char*>(b.data()+data+start),size);
            if(!path.empty() && path.back()==0)path.pop_back();
            std::string_view v=path;if(v.starts_with("sdmc:"))v.remove_prefix(5);
            if(nexus_vsd::Canonical(v) && nexus_vsd::Nro(v))return path;
            return {};
        }
        off+=(32+name+3)&~std::uint64_t{3};
    }
    return {};
}
// A generic sdmc:/ target is NOT evidence of a USB forwarder. Require the
// explicit VSD path and executable-hash contract written by our installer.
inline std::string UsbForwarderPath(std::span<const std::uint8_t> b){
    const auto target=ForwarderPath(b);if(target.empty())return {};
    auto table=Read<std::uint64_t>(b,56),len=Read<std::uint64_t>(b,64),data=Read<std::uint64_t>(b,72);
    if(!Range(table,len,b.size()) || data>b.size())return {};
    std::string usb;bool hash=false,path_seen=false;
    for(std::uint64_t off=0;Range(off,32,len);){
        const auto at=table+off;auto n=Read<std::uint32_t>(b,at+28);
        if(!Range(off+32,n,len))return {};
        const std::string_view name(reinterpret_cast<const char*>(b.data()+at+32),n);
        if(Read<std::uint32_t>(b,at)==0 && (name=="nexusUsbPath" || name=="nexusNroHash")){
            const auto start=Read<std::uint64_t>(b,at+8),size=Read<std::uint64_t>(b,at+16);
            if(!Range(start,size,b.size()-data))return {};
            if(name=="nexusNroHash"){
                if(hash || size!=32)return {};
                hash=true;
                bool nonzero=false;for(size_t i=0;i<32;++i)nonzero|=b[data+start+i]!=0;
                if(!nonzero)return {};
            }else{
                if(path_seen || !size || size>=256)return {};
                path_seen=true;
                usb.assign(reinterpret_cast<const char*>(b.data()+data+start),size);
                if(usb.back()==0)usb.pop_back();
            }
        }
        off+=(32+n+3)&~std::uint64_t{3};
    }
    nexus_vsd::Profile profile{};
    return hash && path_seen && nexus_vsd::Parse(usb,profile) && target==std::string("sdmc:")+profile.logical ? usb:std::string{};
}
}
