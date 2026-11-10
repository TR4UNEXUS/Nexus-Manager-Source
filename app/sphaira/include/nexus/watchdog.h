// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#ifdef __cplusplus
extern "C" {
#endif
// Startup/shutdown guard only. No filesystem, service IPC, reset or unmount.
// It terminates this process, never Horizon or another process.
Result nexusWatchdogStart(u64 timeout_ns);
void nexusWatchdogArm(u64 timeout_ns);
void nexusWatchdogDisarm(void);
void nexusWatchdogStop(void);
// Protect the ENTIRE possible-mutating operation, not individual writes.
void nexusWatchdogProtect(void);
void nexusWatchdogUnprotect(void);
bool nexusWatchdogCanExit(void);
bool nexusWatchdogTryExit(void);
#ifdef __cplusplus
}
#endif
