// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace nexus::smbdiscovery {
using Bytes=std::vector<std::uint8_t>;
inline constexpr size_t MaxReply=64*1024+4,MaxPeers=24;
struct Range {std::uint32_t network{},mask{};bool limited{};std::vector<std::uint32_t> hosts;};
// Input integers are in host order. Discovery stays on the connected private
// LAN; unusual/public/VPN address ranges remain available through manual entry.
inline bool Private(std::uint32_t ip){return (ip>>24)==10 || (ip&0xfff00000)==0xac100000 || (ip&0xffff0000)==0xc0a80000 || (ip&0xffff0000)==0xa9fe0000;}
inline bool Plan(std::uint32_t ip,std::uint32_t mask,Range& out){
    out={};const auto inverse=~mask;
    if(!Private(ip)||!mask||(inverse&(inverse+1))||inverse<3)return false;
    const auto original_network=ip&mask,original_broadcast=original_network|inverse;
    if(ip==original_network||ip==original_broadcast)return false;
    out.limited=mask<0xffffff00u;out.mask=out.limited?0xffffff00u:mask;out.network=ip&out.mask;
    const auto end=out.network|~out.mask;
    for(auto host=out.network+1;host<end;++host)if(host!=ip&&Private(host))out.hosts.push_back(host);
    return !out.hosts.empty()&&out.hosts.size()<=253;
}
inline void Put16(std::span<std::uint8_t> b,size_t p,std::uint16_t v){b[p]=v;b[p+1]=v>>8;}
inline void Put32(std::span<std::uint8_t> b,size_t p,std::uint32_t v){for(size_t i=0;i<4;++i)b[p+i]=v>>(8*i);}
inline std::uint16_t Get16(std::span<const std::uint8_t> b,size_t p){return std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8);}
inline std::uint32_t Get32(std::span<const std::uint8_t> b,size_t p){return Get16(b,p)|(std::uint32_t(Get16(b,p+2))<<16);}
inline bool Dialect(std::uint16_t d){return d==0x0202||d==0x0210||d==0x0300||d==0x0302||d==0x0311;}
// Direct TCP framing plus one SMB2 NEGOTIATE. Offer SMB 2.0.2 through 3.1.1;
// the latter includes its mandatory SHA-512 preauthentication capability.
// Wire definitions: Microsoft MS-SMB2 2.1, 2.2.3 and 2.2.3.1.1.
inline std::array<std::uint8_t,162> Negotiate(const std::array<std::uint8_t,16>& guid,const std::array<std::uint8_t,32>& salt){
    std::array<std::uint8_t,162> b{};b[3]=158;
    b[4]=0xfe;b[5]='S';b[6]='M';b[7]='B';Put16(b,8,64);Put16(b,18,1);
    Put16(b,68,36);Put16(b,70,5);Put16(b,72,1);
    std::copy(guid.begin(),guid.end(),b.begin()+80);
    Put32(b,96,112);Put16(b,100,1);
    constexpr std::array<std::uint16_t,5> dialects{0x0202,0x0210,0x0300,0x0302,0x0311};
    for(size_t i=0;i<dialects.size();++i)Put16(b,104+i*2,dialects[i]);
    Put16(b,116,1);Put16(b,118,38); // preauth context, 38-byte data
    Put16(b,124,1);Put16(b,126,32);Put16(b,128,1); // SHA-512, salt
    std::copy(salt.begin(),salt.end(),b.begin()+130);return b;
}
enum class Reply {More,Valid,Invalid};
inline Reply Response(std::span<const std::uint8_t> b){
    if(b.size()<4)return Reply::More;
    const size_t payload=(size_t(b[1])<<16)|(size_t(b[2])<<8)|b[3],total=payload+4;
    if(b[0]||payload<128||total>MaxReply||b.size()>total)return Reply::Invalid;
    if(b.size()<total)return Reply::More;
    if(b[4]!=0xfe||b[5]!='S'||b[6]!='M'||b[7]!='B'||Get16(b,8)!=64||Get32(b,12)||Get16(b,16)||
       !(Get32(b,20)&1)||(Get32(b,20)&6)||Get32(b,24)||Get32(b,28)||Get32(b,32)||Get32(b,40)||Get32(b,44)||Get32(b,48)||
       Get16(b,68)!=65||!Dialect(Get16(b,72)))return Reply::Invalid;
    const size_t offset=Get16(b,124),length=Get16(b,126);
    if(length&&(offset<128||offset>payload||length>payload-offset))return Reply::Invalid;
    if(Get16(b,72)==0x0311){
        const auto count=Get16(b,74);size_t at=Get32(b,128);bool preauth=false;
        if(!count||count>32||at<128||at%8||at>payload|| (length&&at<offset+length))return Reply::Invalid;
        for(size_t i=0;i<count;++i){
            if(at>payload||payload-at<8)return Reply::Invalid;
            const auto kind=Get16(b,at+4),size=Get16(b,at+6);const size_t data=at+12;
            if(Get32(b,at+8)||size>payload-at-8)return Reply::Invalid;
            if(kind==1){
                if(preauth||size<6||Get16(b,data)!=1||Get16(b,data+4)!=1||Get16(b,data+2)>size-6)return Reply::Invalid;
                preauth=true;
            }
            at=(at+8+size+7)&~size_t(7);
        }
        if(!preauth)return Reply::Invalid;
    }
    return Reply::Valid;
}
}
