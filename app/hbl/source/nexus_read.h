// SPDX-License-Identifier: ISC
#pragma once
#include <switch.h>

// Bound IPC mappings and lock occupancy, for SD and USB alike. No retries or
// padding on short/error reads; a partially loaded executable is never run.
#define NEXUS_NRO_READ_CHUNK (1024u * 1024u)
typedef Result (*NexusReadAt)(void*, u64, void*, size_t, u64*);
static inline Result nexusReadExact(NexusReadAt read_at, void* context, u64 offset,
                                   void* buffer, size_t size) {
    const Result invalid = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (!read_at || (!buffer && size) || size > UINT64_MAX-offset) return invalid;
    size_t done=0;
    while (done<size) {
        const size_t remaining=size-done;
        const size_t count=remaining>NEXUS_NRO_READ_CHUNK ? NEXUS_NRO_READ_CHUNK : remaining;
        u64 got=0;
        const Result rc=read_at(context,offset+done,(u8*)buffer+done,count,&got);
        if (R_FAILED(rc)) return rc;
        if (got!=count) return invalid;
        done+=count;
    }
    return 0;
}
