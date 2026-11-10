#include "nexus/watchdog.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static void (*entry)(void*);
static jmp_buf escape;
static u64 tick;
static int sleeps,created,closed,exited,scenario;
enum { EXPIRE, DISARM, PROTECTED, REARM, CREATE_FAIL, START_FAIL, STOP, STOP_FAIL, MANUAL, MANUAL_PROTECTED };
u64 svcGetSystemTick(void){return tick;}
void svcExitProcess(void){++exited;longjmp(escape,1);}
void svcSleepThread(int64_t ns){
    assert(ns==50000000);tick+=(u64)ns;++sleeps;
    if(scenario==DISARM && sleeps==10)longjmp(escape,2);
    if(scenario==PROTECTED){
        if(sleeps==4){assert(!nexusWatchdogCanExit());nexusWatchdogUnprotect();}
        if(sleeps==8){assert(!nexusWatchdogCanExit());nexusWatchdogUnprotect();assert(nexusWatchdogCanExit());}
    }
    if(scenario==REARM && sleeps==1)nexusWatchdogArm(300000000);
    assert(sleeps<20);
}
Result threadCreate(Thread* t,void(*f)(void*),void* arg,void* stack,size_t size,int priority,int core){
    assert(!arg && stack && ((uintptr_t)stack%4096)==0 && size==0x4000);
    assert(priority==0x2c && core==-2);entry=f;t->handle=42;++created;
    return scenario==CREATE_FAIL?1:0;
}
Result threadStart(Thread* t){assert(t->handle==42);return scenario==START_FAIL?2:0;}
Result threadClose(Thread* t){assert(t->handle==42);++closed;return 0;}
Result waitSingleHandle(Handle h,u64 timeout){
    assert(h==42 && timeout==1000000000);if(scenario==STOP_FAIL)return 3;
    const int before=sleeps;entry(NULL);assert(sleeps==before);return 0;
}
int main(int argc,char** argv){
    assert(argc==2);const char* modes[]={"expiry","disarm","protected","rearm","create-fail","start-fail","stop","stop-fail","manual","manual-protected"};
    for(;scenario<10 && strcmp(argv[1],modes[scenario]);++scenario){}assert(scenario<10);
    const int outcome=setjmp(escape);
    if(!outcome){
        const Result rc=nexusWatchdogStart(100000000);
        if(scenario==CREATE_FAIL || scenario==START_FAIL){assert(rc && closed==(scenario==START_FAIL));nexusWatchdogStop();puts("watchdog failure cleanup passed");return 0;}
        assert(!rc && !nexusWatchdogStart(100000000) && created==1);
        if(scenario==STOP){nexusWatchdogStop();assert(closed==1);assert(!nexusWatchdogStart(100000000));nexusWatchdogStop();assert(closed==2 && created==2);puts("watchdog restart/stop passed");return 0;}
        if(scenario==STOP_FAIL)nexusWatchdogStop();
        if(scenario==MANUAL)nexusWatchdogTryExit();
        if(scenario==MANUAL_PROTECTED){nexusWatchdogProtect();assert(!nexusWatchdogTryExit());nexusWatchdogUnprotect();nexusWatchdogTryExit();}
        if(scenario==DISARM)nexusWatchdogDisarm();
        if(scenario==PROTECTED){nexusWatchdogProtect();nexusWatchdogProtect();}
        entry(NULL);assert(0);
    }
    if(scenario==DISARM){assert(outcome==2 && !exited);}
    else{
        assert(outcome==1 && exited==1);
        if(scenario==EXPIRE)assert(tick==100000000);
        if(scenario==PROTECTED)assert(tick==400000000);
        if(scenario==REARM)assert(tick==350000000);
    }
    puts("production watchdog policy passed");return 0;
}
