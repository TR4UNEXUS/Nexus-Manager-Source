// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace nexus::startup {
// Fixed-width, newline-terminated records overwrite complete old records only.
// Order by seq, not physical line position, after wrap. No truncation/deletion.
struct TraceRing {
    static constexpr std::size_t RecordBytes = 320;
    std::size_t slot{}, capacity;
    constexpr std::size_t Offset() const { return slot * RecordBytes; }
    constexpr void Advance() { slot = (slot + 1) % capacity; }
};
}
