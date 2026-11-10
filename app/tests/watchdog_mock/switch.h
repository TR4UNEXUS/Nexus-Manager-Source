#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint64_t u64;
typedef uint8_t u8;
typedef uint32_t Result;
typedef uint32_t Handle;
typedef struct { Handle handle; } Thread;
#define R_FAILED(rc) ((rc)!=0)
static inline u64 armNsToTicks(u64 ns) { return ns; }
u64 svcGetSystemTick(void);
void svcExitProcess(void);
void svcSleepThread(int64_t ns);
Result threadCreate(Thread*,void(*)(void*),void*,void*,size_t,int,int);
Result threadStart(Thread*);
Result threadClose(Thread*);
Result waitSingleHandle(Handle,u64);
