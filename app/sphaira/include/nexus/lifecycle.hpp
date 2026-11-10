// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
namespace nexus::lifecycle {
inline std::atomic_bool foreground{true}, stopping{false};
inline void SetForeground(bool value) { foreground.store(value); }
inline void BeginShutdown() { stopping.store(true); }
inline bool CanPoll() { return foreground.load() && !stopping.load(); }
}
