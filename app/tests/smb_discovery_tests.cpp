#include "nexus/smb_discovery_core.hpp"
#include <cassert>
#include <iostream>
#include <random>

namespace c=nexus::smbdiscovery;
static c::Bytes Reply(std::uint16_t dialect=0x0302){
    c::Bytes b(dialect==0x0311?178:132);
    b[3]=b.size()-4;b[4]=0xfe;b[5]='S';b[6]='M';b[7]='B';
    c::Put16(b,8,64);c::Put32(b,20,1);c::Put16(b,68,65);c::Put16(b,72,dialect);
    if(dialect==0x0311){
        c::Put16(b,74,1);c::Put32(b,128,128);
        c::Put16(b,132,1);c::Put16(b,134,38);
        c::Put16(b,140,1);c::Put16(b,142,32);c::Put16(b,144,1);
    }
    return b;
}
int main(){
    c::Range r;
    assert(c::Plan(0xc0a80c1a,0xffffff00,r)&&!r.limited&&r.network==0xc0a80c00&&r.hosts.size()==253);
    assert(r.hosts.front()==0xc0a80c01&&r.hosts.back()==0xc0a80cfe);
    assert(std::find(r.hosts.begin(),r.hosts.end(),0xc0a80c1a)==r.hosts.end());
    assert(c::Plan(0x0a020304,0xff000000,r)&&r.limited&&r.network==0x0a020300&&r.mask==0xffffff00&&r.hosts.size()==253);
    assert(c::Plan(0xac120109,0xfffffffc,r)&&r.hosts==std::vector<std::uint32_t>{0xac12010a});
    assert(c::Plan(0xa9fe0202,0xffff0000,r)&&r.limited);
    for(auto ip:{0x7f000001u,0x08080808u,0u,0xffffffffu,0xac0f0002u,0xac200002u,0xc0a80100u,0xc0a801ffu})assert(!c::Plan(ip,0xffffff00,r)&&r.hosts.empty());
    for(auto mask:{0u,0xff00ff00u,0xffffffffu,0xfffffffeu,0x00ffffffu})assert(!c::Plan(0xc0a80102,mask,r)&&r.hosts.empty());
    std::array<std::uint8_t,16> guid{};std::array<std::uint8_t,32> salt{};
    for(size_t i=0;i<guid.size();++i)guid[i]=i+1;for(size_t i=0;i<salt.size();++i)salt[i]=i+0x80;
    const auto request=c::Negotiate(guid,salt);
    assert(request.size()==162&&request[0]==0&&request[3]==158);
    assert(request[4]==0xfe&&request[5]=='S'&&request[6]=='M'&&request[7]=='B');
    assert(c::Get16(request,8)==64&&c::Get16(request,16)==0&&c::Get16(request,18)==1&&c::Get32(request,20)==0);
    assert(c::Get16(request,68)==36&&c::Get16(request,70)==5&&c::Get16(request,72)==1);
    assert(std::equal(guid.begin(),guid.end(),request.begin()+80));
    assert(c::Get32(request,96)==112&&c::Get16(request,100)==1);
    const std::array<std::uint8_t,10> dialect_bytes{2,2,0x10,2,0,3,2,3,0x11,3};
    assert(std::equal(dialect_bytes.begin(),dialect_bytes.end(),request.begin()+104));
    assert(c::Get16(request,116)==1&&c::Get16(request,118)==38&&c::Get32(request,120)==0);
    assert(c::Get16(request,124)==1&&c::Get16(request,126)==32&&c::Get16(request,128)==1);
    assert(std::equal(salt.begin(),salt.end(),request.begin()+130));
    for(auto d:{0x0202,0x0210,0x0300,0x0302,0x0311}){
        auto b=Reply(d);assert(c::Response(b)==c::Reply::Valid);
        for(size_t n=0;n<b.size();++n)assert(c::Response(std::span(b).first(n))==c::Reply::More);
        b.push_back(0);assert(c::Response(b)==c::Reply::Invalid);
    }
    for(auto offset:{0,4,5,6,7,8,12,16,24,28,32,40,44,48,68,72}){
        auto b=Reply();b[offset]^=1;assert(c::Response(b)==c::Reply::Invalid);
    }
    for(auto flags:{0,2,3,5,7}){auto b=Reply();c::Put32(b,20,flags);assert(c::Response(b)==c::Reply::Invalid);}
    for(auto length:{0u,127u,65537u,0xffffffu}){
        std::array<std::uint8_t,4> b{0,std::uint8_t(length>>16),std::uint8_t(length>>8),std::uint8_t(length)};
        assert(c::Response(b)==c::Reply::Invalid);
    }
    auto b=Reply();b.resize(140);b[3]=136;c::Put16(b,124,128);c::Put16(b,126,8);assert(c::Response(b)==c::Reply::Valid);
    for(auto offset:{1,127,129,137,65535}){auto bad=b;c::Put16(bad,124,offset);assert(c::Response(bad)==c::Reply::Invalid);}
    c::Put16(b,126,9);assert(c::Response(b)==c::Reply::Invalid);
    for(auto offset:{0u,120u,129u,176u,0xffffffffu}){b=Reply(0x0311);c::Put32(b,128,offset);assert(c::Response(b)==c::Reply::Invalid);}
    for(auto count:{0,2,33,65535}){b=Reply(0x0311);c::Put16(b,74,count);assert(c::Response(b)==c::Reply::Invalid);}
    for(auto field:{132,134,136,140,142,144}){b=Reply(0x0311);b[field]^=0x40;assert(c::Response(b)==c::Reply::Invalid);}
    b=Reply(0x0311);c::Put16(b,124,128);c::Put16(b,126,1);assert(c::Response(b)==c::Reply::Invalid); // overlapping context
    b=Reply(0x0311);c::Put16(b,134,5);assert(c::Response(b)==c::Reply::Invalid);
    // Valid security blob followed by an aligned mandatory preauth context.
    b=Reply(0x0311);b.insert(b.begin()+132,8,0);b[3]=b.size()-4;c::Put32(b,128,136);c::Put16(b,124,128);c::Put16(b,126,8);
    assert(c::Response(b)==c::Reply::Valid);
    // A second preauth context is invalid, even if all lengths fit.
    b=Reply(0x0311);const c::Bytes context(b.begin()+132,b.end());b.resize(180);b.insert(b.end(),context.begin(),context.end());b[3]=b.size()-4;c::Put16(b,74,2);
    assert(c::Response(b)==c::Reply::Invalid);
    std::mt19937 rng(0x534d42);for(size_t i=0;i<100000;++i){
        auto fuzz=Reply(i%2?0x0311:0x0302);
        for(size_t j=0;j<1+i%8;++j)fuzz[rng()%fuzz.size()]=rng();
        if(i%3==0)fuzz.resize(rng()%fuzz.size());
        (void)c::Response(fuzz);
    }
    std::cout<<"SMB discovery range/wire/parser tests passed\n";
}
