// SPDX-License-Identifier: ISC
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Emitted ONLY for a fresh, physical-SD launch of this app by our embedded
// loader. It is a capability hint, not an authentication boundary.
// Keep this ABI identifier stable across 0.6.6/0.6.7; it is not the app version.
#define NEXUS_SD_BOOTSTRAP_INFO "USB Loader SD bootstrap v2 / 0.6.6"
#define NEXUS_SD_APP_PATH "/switch/nexusmanager/NexusManager.nro"

static inline bool nexusDeferUsbRegistration(bool first_load, const char* path,
                                            const char* initial_usb) {
    return first_load && (!initial_usb || !initial_usb[0]) && path &&
           strcmp(path, NEXUS_SD_APP_PATH) == 0;
}
static inline bool nexusHasSdBootstrap(const char* info, size_t size) {
    return info && size == sizeof(NEXUS_SD_BOOTSTRAP_INFO) &&
           memcmp(info, NEXUS_SD_BOOTSTRAP_INFO, size) == 0;
}
