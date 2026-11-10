// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace nexus::installed_move {
// Source cleanup needs ContentInfo references only. Old forwarders sometimes
// declare a nonexistent ContentMetaInfo dependency after their valid content
// table. That unused tail must not stop removal of a different, verified game.
// No stored metadata is repaired, normalized or rewritten by this reader.
inline constexpr std::size_t ReferencePageSize=64;
inline constexpr std::size_t HeaderSize=8;

inline std::uint16_t Read16(const std::array<std::uint8_t,HeaderSize>& header,std::size_t offset) {
    return std::uint16_t(header[offset])|(std::uint16_t(header[offset+1])<<8);
}

template<class ContentInfo,class Io,class Visit>
std::uint32_t VisitReferences(Io& io,const Visit& visit,std::uint32_t invalid_result) {
    // All operations are read-only. A caller may remove references from its
    // in-memory cleanup plan, but must not delete files unless this succeeds
    // for every other metadata record and all later deletion gates succeed.
    auto rc=io.Check();if(rc)return rc;
    std::uint64_t size{},actual{};
    rc=io.Size(size);if(rc)return rc;
    if(size<HeaderSize)return invalid_result;
    std::array<std::uint8_t,HeaderSize> header{};
    rc=io.Header(header,actual);if(rc)return rc;
    if(actual!=header.size())return invalid_result;
    const std::uint64_t begin=HeaderSize+std::uint64_t(Read16(header,0));
    const auto total=Read16(header,2);
    if(begin>size||std::uint64_t(total)>(size-begin)/sizeof(ContentInfo))return invalid_result;

    std::array<ContentInfo,ReferencePageSize> page{};
    for(std::int32_t offset=0;offset<total;){
        rc=io.Check();if(rc)return rc;
        const auto wanted=std::min<std::size_t>(page.size(),std::size_t(total-offset));
        std::int32_t count{};
        rc=io.Infos(offset,page.data(),wanted,count);if(rc)return rc;
        if(count!=static_cast<std::int32_t>(wanted))return invalid_result;
        for(std::int32_t i=0;i<count;++i)visit(page[std::size_t(i)]);
        offset+=count;
    }
    // Detect a changed declaration between the range reads. The manager still
    // rechecks the moved records, source/destination generation and publication
    // immediately before deleting its verified source candidates.
    rc=io.Check();if(rc)return rc;
    std::uint64_t current_size{};rc=io.Size(current_size);if(rc)return rc;
    std::array<std::uint8_t,HeaderSize> current{};actual=0;
    rc=io.Header(current,actual);if(rc)return rc;
    if(current_size!=size||actual!=current.size()||current!=header)return invalid_result;
    return 0;
}
}
