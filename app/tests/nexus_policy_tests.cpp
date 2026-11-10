// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/nx_Policy.hpp"
#include "nexus/safety.hpp"
#include <cassert>
#include <limits>
#include <iostream>
int main(){
    using namespace nexus;
    const Target all[]={Target::Sd,Target::Internal,Target::Usb};
    unsigned routes=0;
    for(auto from:all)for(auto to:all){
        assert(ValidMove(from,to)==(from!=to));
        routes+=ValidMove(from,to);
        assert(ShareMetadata(from,to)==((from==Target::Internal)==(to==Target::Internal)));
    }
    assert(routes==6);
    assert(!ValidMove(static_cast<Target>(-1),Target::Usb));
    assert(!ValidMove(Target::Sd,static_cast<Target>(3)));
    assert(ShareMetadata(Target::Usb,Target::Sd));
    assert(!ShareMetadata(Target::Usb,Target::Internal));
    struct Key {std::uint64_t id;std::uint32_t version;std::uint8_t type,install_type,padding[2];};
    const Key original{0x0100123456780000ULL,7,0x80,0,{0,0}};
    auto other=original;other.padding[0]=0x81;other.padding[1]=0xFF;
    assert(SameMetaKey(original,other));
    other.id++;assert(!SameMetaKey(original,other));other=original;
    other.version++;assert(!SameMetaKey(original,other));other=original;
    other.type++;assert(!SameMetaKey(original,other));other=original;
    other.install_type++;assert(!SameMetaKey(original,other));
    assert(RegisteredFilePath("/registered/00000000/0123456789abcdef0123456789ABCDEF.nca"));
    for(const auto* p:{"/","/registered/","/registered/name.nca","/registered/../0123456789abcdef0123456789abcdef01.nca",
        "/placehld/0123456789abcdef0123456789abcdef01.nca","/save/0123456789abcdef0123456789abcdef01.nca",
        "/registered/0123456789abcdef0123456789abcdef0g.nca"})assert(!RegisteredFilePath(p));
    assert(SelectSdRoute(Provider::Absent,false)==SdRoute::Stock);
    assert(SelectSdRoute(Provider::Present,true)==SdRoute::Native);
    assert(SelectSdRoute(Provider::Present,false)==SdRoute::Unavailable);
    assert(SelectSdRoute(Provider::Unknown,true)==SdRoute::Unavailable);
    assert(ValidRange(10,0,10));assert(ValidRange(10,10,0));
    assert(!ValidRange(10,11,0));assert(!ValidRange(10,9,2));
    assert(!ValidRange(UINT64_MAX,UINT64_MAX,1));
    assert(VolumePathValid("/"));assert(VolumePathValid("/Games/Example.nsp"));
    for(const auto* p:{"","relative","/..","/a/../b","/./x","/a//b","/a/","/a\\b","/sdmc:x","/\nfile"})assert(!VolumePathValid(p));
    assert(!VolumePathValid(std::string("/")+std::string(0x301,'x')));
    std::string path;
    assert(ContentPath("@SdCardContent:/placehld/aa/test.nca",true,path));
    assert(path=="/placehld/aa/test.nca");
    assert(!ContentPath("@SdCardContent:/registered/aa/test.nca",true,path));
    assert(!ContentPath("@UserContent:/placehld/test.nca",true,path));
    assert(!ContentPath("@SdCardContent:/placehld/../registered/test.nca",true,path));
    assert(ContentPath("@SdCardContent://registered/aa/test.nca",false,path));
    assert(SamePlaceholderSnapshot(7,7,1024,1024,true));
    assert(!SamePlaceholderSnapshot(7,8,1024,1024,true));
    assert(!SamePlaceholderSnapshot(7,7,1024,2048,true));
    assert(!SamePlaceholderSnapshot(0,0,-1,-1,true));
    assert(!SamePlaceholderSnapshot(0,0,10,10,false));
    std::cout << "USB Loader policy tests passed: all six move directions, shared metadata, exact delete paths, routing, bounds and reconnects\n";
}
