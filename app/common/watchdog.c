// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/watchdog.h"
#include <stdatomic.h>

static Thread worker;
static _Alignas(0x1000) u8 stack[0x4000];
static u64 deadline;
static unsigned protected_operations;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static atomic_bool stop;
static bool started;

// Serialize deadline changes, write admission and termination. A writer cannot
// begin between the zero-writers check and process exit; disarm wins if first.
static void Lock(void) { while(atomic_flag_test_and_set(&gate)) svcSleepThread(0); }
static void Unlock(void) { atomic_flag_clear(&gate); }
bool nexusWatchdogCanExit(void) {
    Lock();const bool can=protected_operations==0;Unlock();return can;
}
bool nexusWatchdogTryExit(void) {
    Lock();
    const bool can=protected_operations==0;
    if (can) svcExitProcess(); // retains admission lock until process is gone
    Unlock();return can;
}
void nexusWatchdogProtect(void) { Lock();++protected_operations;Unlock(); }
void nexusWatchdogUnprotect(void) { Lock();if(protected_operations)--protected_operations;Unlock(); }
void nexusWatchdogDisarm(void) { Lock();deadline=0;Unlock(); }
void nexusWatchdogArm(u64 timeout_ns) {
    Lock();deadline=svcGetSystemTick()+armNsToTicks(timeout_ns);Unlock();
}
static void Run(void* unused) {
    (void)unused;
    while (!atomic_load(&stop)) {
        Lock();
        if (deadline && svcGetSystemTick()>=deadline && !protected_operations)
            svcExitProcess();
        Unlock();
        svcSleepThread(50*1000*1000);
    }
}
Result nexusWatchdogStart(u64 timeout_ns) {
    if (started) return 0;
    atomic_store(&stop,false);
    nexusWatchdogArm(timeout_ns);
    // Explicit stack avoids allocation in HBL, whose malloc heap is disabled.
    Result rc=threadCreate(&worker,Run,NULL,stack,sizeof(stack),0x2c,-2);
    if (R_FAILED(rc)) { nexusWatchdogDisarm(); return rc; }
    rc=threadStart(&worker);
    if (R_FAILED(rc)) { threadClose(&worker); nexusWatchdogDisarm(); return rc; }
    started=true;
    return 0;
}
void nexusWatchdogStop(void) {
    if (!started) return;
    nexusWatchdogDisarm();
    atomic_store(&stop,true);
    // Never leave an HBL/NRO watchdog executing from code about to be unloaded.
    if (R_FAILED(waitSingleHandle(worker.handle,1000*1000*1000ULL))) {
        // Callers only stop after all possible-mutating operations are joined.
        // If that contract was broken, do not kill a writer or unmap this code.
        if (!nexusWatchdogTryExit()) {
            while (!nexusWatchdogTryExit()) svcSleepThread(50*1000*1000);
        }
        return;
    }
    threadClose(&worker);
    started=false;
}
