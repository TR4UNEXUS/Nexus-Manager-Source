// SPDX-License-Identifier: ISC
#pragma once
#include <switch.h>
Result nexusVsdBeforeLoad(Handle process,const char* logical,const char* initial_usb);
typedef struct {
    Service service;
    bool sm_open, service_open;
    u64 generation, size;
    char physical[256];
} NexusUsbSource;
// Read through the shared owner BEFORE activating the sdmc: view. No raw
// usb:hs ownership, second mount, writes or global routing changes.
Result nexusUsbOpen(NexusUsbSource* source,const char* physical);
Result nexusUsbRead(void* source,u64 offset,void* buffer,size_t size,u64* actual);
Result nexusUsbActivate(NexusUsbSource* source,Handle process,const char* logical);
void nexusUsbClose(NexusUsbSource* source);
