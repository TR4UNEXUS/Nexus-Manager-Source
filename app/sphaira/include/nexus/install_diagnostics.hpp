// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace nexus::install {
// Caller-owned, one attempt only. Never persisted as an exclusion/blacklist.
enum class Failure { Package, Content, MissingContent, Size, Hash, Metadata, Header, ExpandedSize, EmptyHeader, UndecodableHeader, Publication };
// Worker-owned facts; includes decode failures before the size is available.
enum class HeaderSizeIssue { None, Invalid, Metadata, Source, Empty, Undecodable };
inline Failure HeaderFailure(HeaderSizeIssue issue) {
    if(issue==HeaderSizeIssue::Empty)return Failure::EmptyHeader;
    if(issue==HeaderSizeIssue::Undecodable)return Failure::UndecodableHeader;
    if(issue==HeaderSizeIssue::Invalid)return Failure::Header;
    return Failure::ExpandedSize;
}
inline HeaderSizeIssue CheckHeaderSize(std::uint64_t declared, std::int64_t entry,
        bool compressed, bool has_metadata, std::uint64_t expected) {
    if(declared<0xc00 || declared>INT64_MAX || entry<0xc00) return HeaderSizeIssue::Invalid;
    if(has_metadata && declared!=expected) return HeaderSizeIssue::Metadata;
    if(!compressed && declared!=std::uint64_t(entry)) return HeaderSizeIssue::Source;
    return HeaderSizeIssue::None;
}
template<class Entries> bool HasRecognizedMetadata(const Entries& entries) {
    // Match installer dispatch exactly; do not infer ownership from filenames.
    for(const auto& entry:entries)
        if(entry.name.ends_with(".cnmt.nca") || entry.name.ends_with(".cnmt.ncz"))return true;
    return false;
}
inline std::string ContentName(std::string_view name) {
    if(name.size()!=36 && name.size()!=41)return {};
    std::string out(name);
    for(auto& c:out)if(c>='A' && c<='Z')c+=32;
    for(unsigned i=0;i<32;++i)if(!((out[i]>='0' && out[i]<='9') || (out[i]>='a' && out[i]<='f')))return {};
    const auto suffix=std::string_view(out).substr(32);
    return suffix==".nca" || suffix==".ncz" || suffix==".cnmt.nca" || suffix==".cnmt.ncz" ? out:std::string{};
}
inline bool RawSizeMismatch(std::string_view name,std::int64_t actual,std::uint64_t expected) {
    const auto safe=ContentName(name);
    // Compressed NCZ bytes must NEVER be compared to expanded CNMT size.
    return !safe.empty() && safe.ends_with(".nca") && (actual<0 || std::uint64_t(actual)!=expected);
}
struct Diagnostic {
    std::string text;
    std::string completion;
    void Clear(){text.clear();completion.clear();}
    void Set(Failure failure,std::string_view file={},std::uint64_t title=0,
             std::uint64_t expected=0,std::int64_t actual=0) {
        switch(failure) {
            case Failure::Package:text="Package could not be read or its layout was rejected. Check the source file and connection."_nx;break;
            case Failure::Content:text="NCA install/validation failed. This result can also mean a read, write, key or size problem; it is not proof of damaged media."_nx;break;
            case Failure::Publication:text="Content publication/registration failed. Keep the original source and exact error/log. For USB, matching P2 firmware is required; verified bytes alone do not confirm registration."_nx;break;
            case Failure::MissingContent:text="Required content could not be resolved safely in this package. Check that the package is complete."_nx;break;
            case Failure::Size:text="NCA size disagrees with this package's content metadata. Replace/recheck the source package; do not format the drive."_nx;break;
            case Failure::Hash:text="NCA source hash does not match the expected content hash (including supported distribution conversion). Recheck/replace the source package; check the USB connection if repeated reads differ."_nx;break;
            case Failure::Metadata:text="No recognized content metadata (.cnmt.nca/.cnmt.ncz). This package was not installed. Obtain a complete, correctly packaged source; do not guess NCA names."_nx;break;
            case Failure::Header:text="Decoded NCA header declares an invalid size. Recheck/replace the source package; do not format the drive."_nx;break;
            case Failure::ExpandedSize:text="Decoded NCA size disagrees with its expected uncompressed size. Recheck/replace the source package; compressed file size is not used as expanded size."_nx;break;
            case Failure::EmptyHeader:text="NCA header read is entirely zero (first 3072 bytes). Recheck the source package and connection. Adding keys cannot restore missing header data; do not format the drive."_nx;break;
            case Failure::UndecodableHeader:text="NCA header could not be decoded. Recheck the source package and matching keys; this alone does not prove corruption. Do not format the drive."_nx;break;
        }
        const auto name=ContentName(file);
        if(!name.empty())text+="\nNCA: "+name;
        if(title){char id[17]{};std::snprintf(id,sizeof(id),"%016llX",static_cast<unsigned long long>(title));text+="\nTitle ID: "_nx+std::string(id);}
        if(failure==Failure::Size)text+="\nExpected bytes: "_nx+std::to_string(expected)+"\nPackage entry bytes: "_nx+std::to_string(actual);
        if(failure==Failure::ExpandedSize)text+="\nExpected expanded bytes: "_nx+std::to_string(expected);
        if(failure==Failure::ExpandedSize || failure==Failure::Header)text+="\nNCA header declared bytes: "_nx+std::to_string(std::uint64_t(actual));
        text+="\nNo permanent block: a replacement/reinstall is checked afresh."_nx;
    }
};
}
