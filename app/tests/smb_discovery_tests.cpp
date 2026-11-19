#include "nexus/smb_discovery_core.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <set>

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
    assert(!r.hosts.contains(0xc0a80c1a));
    assert(c::Plan(0x0a020304,0xff000000,r)&&!r.limited&&r.network==0x0a000000&&r.mask==0xff000000&&r.hosts.size()==16777213);
    assert(c::Plan(0xac120109,0xfffffffc,r)&&r.hosts.size()==1&&r.hosts[0]==0xac12010a);
    assert(c::Plan(0xa9fe0202,0xffff0000,r)&&!r.limited&&r.hosts.size()==65533);
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
    // Independent SMB 2.1 NAS reply captured during the live regression check.
    // Server GUID and timestamps are cleared; no login or share access occurred.
    const std::string nas_hex="000000cafe534d42400000000000000000000100010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000410001001002000000000000000000000000000000000000070000000000800000008000000080000000000000000000000000000000000080004a0000000000604806062b0601050502a03e303ca00e300c060a2b06010401823702020aa32a3028a0261b246e6f745f646566696e65645f696e5f5246433431373840706c656173655f69676e6f7265";
    c::Bytes nas;for(size_t i=0;i<nas_hex.size();i+=2)nas.push_back(std::stoul(nas_hex.substr(i,2),nullptr,16));
    assert(c::Get16(nas,72)==0x0210&&c::Response(nas)==c::Reply::Valid);
    for(size_t cut=0;cut<nas.size();++cut)assert(c::Response(std::span(nas).first(cut))==c::Reply::More);
    // NFS TCP fragments, wrong XID, truncated/auth-denied and unrelated RPC.
    const auto request_nfs=c::NfsNull(0x12345678);assert(c::Be32(request_nfs,0)==0x80000028&&c::Be32(request_nfs,16)==100003&&c::Be32(request_nfs,20)==3);
    c::Bytes nfs(28);c::PutBe32(nfs,0,0x80000018);c::PutBe32(nfs,4,0x12345678);c::PutBe32(nfs,8,1);
    assert(c::NfsResponse(nfs,0x12345678)==c::Reply::Valid);
    for(size_t cut=0;cut<nfs.size();++cut)assert(c::NfsResponse(std::span(nfs).first(cut),0x12345678)==c::Reply::More);
    assert(c::NfsResponse(nfs,99)==c::Reply::Invalid);
    for(auto off:{4,8,12,20,24}){auto bad=nfs;bad[off]^=0xff;assert(c::NfsResponse(bad,0x12345678)==c::Reply::Invalid);}
    auto fragments=nfs;fragments.erase(fragments.begin(),fragments.begin()+4);fragments.insert(fragments.begin()+8,{0x80,0,0,16});fragments.insert(fragments.begin(),{0,0,0,8});
    assert(c::NfsResponse(fragments,0x12345678)==c::Reply::Valid);
    for(size_t cut=0;cut<fragments.size();++cut)assert(c::NfsResponse(std::span(fragments).first(cut),0x12345678)==c::Reply::More);
    auto mismatch=nfs;mismatch.resize(36);c::PutBe32(mismatch,0,0x80000020);c::PutBe32(mismatch,24,2);c::PutBe32(mismatch,28,4);c::PutBe32(mismatch,32,4);assert(c::NfsResponse(mismatch,0x12345678)==c::Reply::Valid);
    c::PutBe32(mismatch,24,1);assert(c::NfsResponse(mismatch,0x12345678)==c::Reply::Invalid);
    // Entire /23 and /16, with no duplicate/self/network/broadcast probes.
    for(auto mask:{0xfffffe00u,0xffff0000u}){assert(c::Plan(0xc0a80105,mask,r));std::set<std::uint32_t> seen;
        for(size_t i=0;i<r.hosts.size();++i){const auto host=r.hosts[i];assert(r.hosts.contains(host)&&seen.insert(host).second);}
        assert(seen.size()==size_t(~mask)-2);
    }
    std::cout<<"SMB discovery range/wire/parser tests passed\n";
}
