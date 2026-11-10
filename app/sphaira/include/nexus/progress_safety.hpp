// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

namespace nexus::safety {
enum class Operation { MayWrite, ReadOnly };
// Cancellation is a REQUEST, not completion. Keep the worker and its buffers
// alive until it actually returns. A late reply must never target freed memory.
struct ProgressState {
    std::atomic_bool finished{};
    std::atomic<std::uint64_t> last_progress{};
    void Touch(std::uint64_t now) { last_progress.store(now); }
    void Finish() { finished.store(true); }
    bool CanDismiss() const { return finished.load(); }
    bool Slow(std::uint64_t now,std::uint64_t interval) const {
        const auto last=last_progress.load();
        return !CanDismiss() && now>=last && now-last>=interval;
    }
    bool CanClose(Operation op,bool no_mutations) const {
        return op==Operation::ReadOnly && no_mutations && !CanDismiss();
    }
};
}
