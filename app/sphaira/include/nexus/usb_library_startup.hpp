// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <cstddef>
#include <string>

namespace sphaira::nexusapp {
struct UsbLibraryStartupResult {
    Result result{};
    bool pending{},console_backup_available{};
    std::size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{};
    std::string detail;
};
// Main-thread API. One read-only attempt per Manager launch; never opens UI.
void StartUsbLibraryStartupCheck();
// Returns a completed result once, including a quiet empty/healthy result.
// No scan or repair plan survives this call. Review must perform a fresh scan.
bool TakeUsbLibraryStartupCheck(UsbLibraryStartupResult&);
// Atomic cancellation only: safe in focus/exit callbacks. No waiting there.
void CancelUsbLibraryStartupCheck();
// Cancel, join and discard before foreground work or service teardown. This
// also suppresses an attempt that has not started, including manual repair.
void StopUsbLibraryStartupCheck();
}
