// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include "nexus/archive_names.hpp"

namespace nexus::compat {
inline int Hex(char c) {
    return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
}
inline bool ContentName(std::string_view name, void* id, bool compressed = true) {
    if (name.size() < 36) return false;
    const auto suffix = name.substr(32);
    if (suffix != ".nca" && suffix != ".cnmt.nca" && !(compressed && (suffix == ".ncz" || suffix == ".cnmt.ncz"))) return false;
    auto* bytes = static_cast<uint8_t*>(id);
    for (size_t i=0; i<16; ++i) {
        const int hi=Hex(name[i*2]), lo=Hex(name[i*2+1]);
        if (hi<0 || lo<0) return false;
        bytes[i]=static_cast<uint8_t>((hi<<4)|lo);
    }
    return true;
}
// C3: some NSZ writers omit only the final name's terminator. Never search
// past the declared table, accept arbitrary tails, or change payload offsets.
inline bool PfsName(std::string_view table, size_t offset, bool final_unique_highest,
                    std::string_view& out, bool& unterminated) {
    out = {}; unterminated = false;
    if (offset >= table.size()) return false;
    const auto end = table.find('\0', offset);
    out = table.substr(offset, end == std::string_view::npos ? table.size()-offset : end-offset);
    if (out.empty() || out == "." || out == ".." || out.find_first_of("/\\:") != std::string_view::npos) return false;
    if (end != std::string_view::npos) return true;
    if (!final_unique_highest || (offset && table[offset-1] != '\0')) return false;
    if (archive::Canonical(out).empty()) return false;
    unterminated = true;
    return true;
}
// A forward cursor is linear over the ENTIRE file, not per output chunk.
template<class Ranges> const typename Ranges::value_type* FindForward(const Ranges& ranges, size_t& cursor, uint64_t offset) {
    while (cursor < ranges.size()) {
        const auto& range=ranges[cursor];
        if (offset < range.offset) return nullptr;
        if (offset-range.offset < range.size) return &range;
        ++cursor;
    }
    return nullptr;
}
constexpr uint64_t MaxSections=262144, MaxBlocks=1048576;
inline bool TableFits(uint64_t count, uint64_t width, uint64_t remaining, uint64_t limit) {
    return count && count<=limit && count<=remaining/width;
}
template<class Sections> bool ValidSections(const Sections& sections, uint64_t nca_size) {
    // C4: NCZ preserves [0,0x4000) verbatim even if an NCA section starts
    // earlier. Validate the FULL table from the NCA header boundary, including
    // ranges hidden by that prefix; FindForward clips by the output position.
    // Never relax overlap, overflow, crypto or final-length checks.
    uint64_t end=0xc00;
    if (sections.empty() || sections.size()>MaxSections || nca_size<0x4000) return false;
    for (const auto& section: sections) {
        if (!section.size || section.offset<end || section.offset>nca_size || section.size>nca_size-section.offset) return false;
        if (section.crypto_type!=1 && section.crypto_type!=3 && section.crypto_type!=4) return false;
        if (section.crypto_type!=1 && (section.offset&15)) return false;
        end=section.offset+section.size;
    }
    return end==nca_size;
}
}
