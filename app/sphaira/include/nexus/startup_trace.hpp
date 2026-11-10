// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>

namespace nexus::startup {
// Diagnostic only. No USB/control requests, resets or changed call results.
// Successful routine USB events flush in bounded groups of 16 or at the next
// non-routine event/error/exit. A power loss can lose at most 15 such events.
void Open();
void Close();
// Switch to the separately reserved shutdown log after workers stop admission.
void BeginShutdown();
// No IPC, no lock: high 32 bits = failed logger operation, low 32 = Result.
// 1=SD open, 2=create log, 3=open log, 4=write log. Zero = no recorded failure.
u64 FailureInfo();
void Event(const char* stage, const char* phase = "point", Result rc = 0, u64 value = 0);
template<typename F> Result Call(const char* stage, F&& operation, u64 value = 0) {
    Event(stage, "begin", 0, value);
    const Result rc = operation();
    Event(stage, "end", rc, value);
    return rc;
}
template<typename F> void CallVoid(const char* stage, F&& operation) {
    Event(stage, "begin");
    operation();
    Event(stage, "end");
}
}
