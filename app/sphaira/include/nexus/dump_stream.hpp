// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::dump {
// Bound filenames independently of the fixed FS path buffer. ASCII replacement
// is deliberate: no broken multibyte suffixes, FAT reserved characters or paths.
inline std::string SafeName(std::string_view name) {
    std::string out;
    for (unsigned char c : name) {
        if (out.size() == 64) break;
        if (c < 32 || c >= 127 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
        out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "Game"_nx : out;
}
// All offsets stay signed-64 representable (libnx). Never trust overflowed sizes.
inline bool AddSize(std::uint64_t& total, std::uint64_t size) {
    constexpr std::uint64_t limit = INT64_MAX;
    if (total > limit || size > limit - total) return false;
    total += size;
    return true;
}
// Shared by production write and verification passes. Exact reads only; errors
// and cancellation propagate before consuming a short/failed chunk.
template<class Cancel, class Read, class Consume, class Progress>
std::uint32_t Stream(std::uint64_t size, std::vector<std::uint8_t>& buffer,
    Cancel cancel, Read read, Consume consume, Progress progress, std::uint32_t invalid) {
    if (buffer.empty() || size > INT64_MAX) return invalid;
    for (std::uint64_t off = 0; off < size;) {
        if (auto rc = cancel()) return rc;
        const auto count = std::min<std::uint64_t>(buffer.size(), size - off);
        std::uint64_t actual{};
        if (auto rc = read(off, buffer.data(), count, actual)) return rc;
        if (actual != count) return invalid;
        if (auto rc = consume(off, buffer.data(), count)) return rc;
        off += count;
        progress(off, size);
    }
    return cancel();
}
}
