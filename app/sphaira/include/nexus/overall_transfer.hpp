// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace nexus::safety {
// Two byte passes per file: copy + full readback, or compare two existing files.
// Reserve ~5% for metadata/publication/source cleanup. Never report 100% early.
class OverallTransfer {
    std::uint64_t work{}, reserve{}, copied{}, finishing{};
public:
    bool Begin(std::uint64_t bytes) {
        if (!bytes || bytes > std::uint64_t(std::numeric_limits<std::int64_t>::max())/4) return false;
        work=bytes*2;reserve=(work+18)/19;copied=finishing=0;return true;
    }
    bool Advance(std::uint64_t bytes) {
        if (!work || bytes>work-copied) return false;
        copied+=bytes;return true;
    }
    bool Finish(unsigned percent) {
        if (!work || copied!=work || percent>100) return false;
        const auto next=(reserve/100)*percent+(reserve%100)*percent/100;
        if(next>finishing)finishing=next;
        return true;
    }
    std::uint64_t Offset() const {return copied+finishing;}
    std::uint64_t Total() const {return work+reserve;}
};
}
