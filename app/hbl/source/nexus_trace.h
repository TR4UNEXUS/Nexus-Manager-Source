// SPDX-License-Identifier: ISC
#pragma once
#include <switch.h>
void nexusTraceOpen(void);
// Resolve the RomFS opt-in before any physical-SD trace I/O.
void nexusTraceConfigure(bool file_logging);
void nexusTrace(const char* stage, Result result, u64 value);
void nexusTraceClose(void);
