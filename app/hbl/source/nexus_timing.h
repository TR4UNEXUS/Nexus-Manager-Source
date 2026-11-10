// SPDX-License-Identifier: ISC
#pragma once
#include <switch.h>
#include "nexus/startup_timing_protocol.h"
void nexusTimingEnter(void);
void nexusTimingConfigure(bool enabled,bool usb,const char* path);
u64 nexusTimingBegin(void);
void nexusTimingEnd(enum NexusStartupStage stage,u64 started);
void nexusTimingBytes(u64 bytes);
void nexusTimingHash(const u8 hash[32],bool matched);
Result nexusTimingSubmit(bool handoff,Result result);
