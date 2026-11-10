#pragma once
#include "../hbl_mock/switch.h"
typedef int Mutex;
enum { CUR_THREAD_HANDLE = 0xFFFE };
bool mutexTryLock(Mutex*);
void mutexUnlock(Mutex*);
Result svcGetThreadId(u64*, Handle);
