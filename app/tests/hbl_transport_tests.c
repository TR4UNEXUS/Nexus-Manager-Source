#include "switch.h"
#include "nexus/vsd_protocol.h"
#include "nexus_vsd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int present,old,ready=1,arm_error,activate=1,refs,begin_count,arm_count,sleeps;
static char begin_paths[2][256];
void nexusTrace(const char* stage,Result result,u64 value){(void)stage;(void)result;(void)value;}
Result smInitialize(void){++refs;return 0;}void smExit(void){--refs;}
Result smGetService(Service* s,const char* n){assert(!strcmp(n,"ams:usb"));s->active=1;return 0;}
void serviceClose(Service* s){s->active=0;}SmServiceName smEncodeName(const char* n){assert(!strcmp(n,"ams:usb"));return 1;}
bool hosversionAtLeast(int a,int b,int c){(void)a;(void)b;(void)c;return true;}
void svcSleepThread(uint64_t n){assert(n==100000000);++sleeps;}
Result mockDispatch(int cmd,const void* in,size_t len,void* out,size_t outlen,Params p){
    if(cmd==65100){*(u8*)out=present;return 0;}
    if(cmd==0){assert(outlen==16);memset(out,0,16);u8* b=out;b[0]=b[1]=ready;b[2]=ready?4:1;((u64*)out)[1]=17;return 0;}
    assert(p.in_send_pid);assert(in && ((const u64*)in)[0]==0);
    if(cmd==NEXUS_VSD_ARM){assert(len==16);assert(((const u64*)in)[1]==17);assert(p.buffers[0].size==strlen(p.buffers[0].ptr)+1);++arm_count;return arm_error?123:0;}
    assert(len==8 && outlen==32);NexusVsdStatus* s=out;*s=(NexusVsdStatus){.magic=old?0:NEXUS_VSD_MAGIC,.version=1,.flags=1,.reserved=1};
    if(cmd==NEXUS_VSD_BEGIN_LOAD){
        assert(p.in_num_handles==1 && p.in_handles[0]==42);assert(p.buffers[0].size==strlen(p.buffers[0].ptr)+1);
        if(begin_count<2)strcpy(begin_paths[begin_count],p.buffers[0].ptr);++begin_count;
        if(arm_count && activate){s->flags=5;s->generation=17;}
    }else assert(cmd==NEXUS_VSD_QUERY);
    return 0;
}
static void reset(void){begin_count=arm_count=sleeps=0;arm_error=0;activate=ready=1;}
int main(void){
    const char* path="/switch/nfsmw-nx/nfsmw-nx.nro";
    assert(!nexusVsdBeforeLoad(42,"/switch/usbloader/USBLoader.nro",NULL)); // ordinary SD without service
    assert(nexusVsdBeforeLoad(42,path,path)); // USB NEVER falls back
    present=1;old=1;assert(!nexusVsdBeforeLoad(42,path,NULL));assert(nexusVsdBeforeLoad(42,path,path));old=0;
    reset();assert(!nexusVsdBeforeLoad(42,path,path));assert(begin_count==2 && arm_count==1);
    assert(!strcmp(begin_paths[0],"/") && !strcmp(begin_paths[1],path));assert(refs==0);
    reset();arm_error=1;assert(nexusVsdBeforeLoad(42,path,path));assert(begin_count==1);
    reset();activate=0;assert(nexusVsdBeforeLoad(42,path,path));
    reset();ready=0;assert(nexusVsdBeforeLoad(42,path,path));assert(!arm_count && sleeps==50);
    reset();assert(!nexusVsdBeforeLoad(42,"/switch/usbloader/USBLoader.nro",NULL));assert(begin_count==1 && !arm_count);
    present=0;assert(nexusVsdBeforeLoad(42,path,NULL));assert(refs==0);
    puts("Real embedded loader transport: PID/handle ABI, bootstrap, no SD fallback, bounded wait and return clearing passed");
}
