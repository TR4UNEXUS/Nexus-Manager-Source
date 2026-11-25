// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include "nexus/awake_policy.hpp"
namespace nexus::power {
inline Mutex mutex{};
inline AwakePolicy policy;
inline void Refresh() {
    mutexLock(&mutex);
    appletSetAutoSleepDisabled(policy.Disabled());
    mutexUnlock(&mutex);
}
inline void Focus(bool foreground) {
    mutexLock(&mutex);
    policy.Focus(foreground);
    appletSetAutoSleepDisabled(policy.Disabled());
    mutexUnlock(&mutex);
}
inline void Stop() {
    mutexLock(&mutex);
    policy.Stop();
    appletSetAutoSleepDisabled(false);
    mutexUnlock(&mutex);
}
}
