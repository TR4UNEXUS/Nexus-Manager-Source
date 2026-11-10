// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexus::manager {
// Validate before forming FsPath (whose legacy constructor has no bounds check).
inline bool SafeRelative(std::string_view path) {
    if(path.empty() || path.size()>640 || path.front()=='/' || path.back()=='/')return false;
    std::size_t start=0;
    while(start<path.size()) {
        const auto end=path.find('/',start),n=(end==path.npos?path.size():end)-start;
        const auto part=path.substr(start,n);
        if(!n || n>255 || part=="." || part==".." || part.back()=='.' || part.back()==' ')return false;
        for(unsigned char c:part)if(c<32 || c==127 || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
        if(end==path.npos)return true;
        start=end+1;
    }
    return false;
}
// No delete/rename/metadata APIs are available to this copy primitive. It only
// returns success after every destination byte has been read back and hashed.
template<class IO> auto CopyVerified(IO& io,std::uint64_t size,void* buffer,std::size_t capacity) {
    using Result=typename IO::Result;
    if(!buffer || !capacity)return Result{IO::Invalid};
    typename IO::Hash source,destination;
    for(std::uint64_t off=0;off<size;) {
        if(auto rc=io.Cancelled())return rc;
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(capacity,size-off));
        std::uint64_t actual{};
        if(auto rc=io.ReadSource(off,buffer,count,actual))return rc;
        if(actual!=count)return Result{IO::Invalid};
        source.Update(buffer,count);
        if(auto rc=io.WriteDestination(off,buffer,count))return rc;
        off+=count;io.Advance(count);
    }
    if(auto rc=io.FlushDestination())return rc;
    for(std::uint64_t off=0;off<size;) {
        if(auto rc=io.Cancelled())return rc;
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(capacity,size-off));
        std::uint64_t actual{};
        if(auto rc=io.ReadDestination(off,buffer,count,actual))return rc;
        if(actual!=count)return Result{IO::Invalid};
        destination.Update(buffer,count);
        off+=count;io.Advance(count);
    }
    if(source.Final()!=destination.Final())return Result{IO::Invalid};
    if constexpr(requires{io.RecordDigest(source.Final());})io.RecordDigest(source.Final());
    return io.Cancelled();
}
}
