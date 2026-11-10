#include "nexus_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static u64 tick;static int refs,opened,closed,submits,present=1,submit_error;
static NexusStartupLoader captured;
u64 svcGetSystemTick(void){return tick;}
u64 armTicksToNs(u64 ticks){return ticks*10;}
Result smInitialize(void){++refs;return 0;}void smExit(void){--refs;}
Result smGetService(Service* s,const char* name){assert(!strcmp(name,"ams:usb"));s->active=1;++opened;return 0;}
void serviceClose(Service* s){assert(s->active);s->active=0;++closed;}
SmServiceName smEncodeName(const char* name){assert(!strcmp(name,"ams:usb"));return 1;}
bool hosversionAtLeast(int a,int b,int c){(void)a;(void)b;(void)c;return true;}
Result mockDispatch(int command,const void* in,size_t len,void* out,size_t outlen,Params p){
    if(command==65100){assert(outlen==1);*(u8*)out=present;return 0;}
    assert(command==NEXUS_STARTUP_SUBMIT && len==8 && outlen==8);
    assert(p.in_send_pid && !p.in_num_handles && *(const u64*)in==0);
    assert(p.buffer_attrs[0]==(SfBufferAttr_HipcMapAlias|SfBufferAttr_In));
    assert(p.buffers[0].size==sizeof(captured));
    memcpy(&captured,p.buffers[0].ptr,sizeof(captured));++submits;
    *(u64*)out=123;return submit_error;
}
int main(void){
    tick=10;nexusTimingEnter();nexusTimingConfigure(true,false,"sdmc:/switch/test.nro");
    tick=20;const u64 begin=nexusTimingBegin();tick=70;nexusTimingEnd(NexusStartup_Open,begin);
    nexusTimingBytes(1024);tick=100;assert(!nexusTimingSubmit(true,0));
    assert(captured.magic==NEXUS_STARTUP_MAGIC && captured.version==1 && captured.source==NEXUS_STARTUP_SOURCE_SD);
    assert(captured.flags==NEXUS_STARTUP_HANDOFF && captured.result==0 && captured.nro_bytes==1024);
    assert(captured.duration_ns[NexusStartup_Open]==500 && captured.duration_ns[NexusStartup_Total]==900);
    assert(captured.duration_ns[NexusStartup_Hash]==0 && !memcmp(captured.executable_sha256,(u8[32]){0},32));
    assert(!strcmp(captured.path,"/switch/test.nro") && refs==0 && opened==closed);
    nexusTimingConfigure(true,true,"/switch/test.nro");u8 hash[32];memset(hash,0xA7,sizeof(hash));
    nexusTimingHash(hash,true);assert(!nexusTimingSubmit(true,0));
    assert(captured.source==NEXUS_STARTUP_SOURCE_USB && captured.flags==7 && !memcmp(captured.executable_sha256,hash,32));
    nexusTimingConfigure(true,true,"/switch/test.nro");nexusTimingHash(hash,false);
    assert(!nexusTimingSubmit(false,17) && captured.flags==NEXUS_STARTUP_HASH_PERFORMED && captured.result==17);
    const int count=submits;nexusTimingConfigure(false,false,NULL);assert(!nexusTimingSubmit(true,0) && submits==count);
    char long_path[257];memset(long_path,'x',sizeof(long_path));long_path[0]='/';long_path[256]=0;
    nexusTimingConfigure(true,false,long_path);assert(nexusTimingSubmit(true,0) && submits==count);
    nexusTimingConfigure(true,false,"relative.nro");assert(nexusTimingSubmit(true,0) && submits==count);
    nexusTimingConfigure(true,false,"/switch/test.nro");present=0;
    assert(nexusTimingSubmit(true,0) && refs==0 && submits==count && opened==closed);
    present=1;submit_error=29;assert(nexusTimingSubmit(true,0)==29 && refs==0 && opened==closed);
    puts("Production HBL timing: ns conversion, opt-in, SD hash skipped, USB hash identity, PID/buffer ABI, failure handling and balanced services passed");
}
