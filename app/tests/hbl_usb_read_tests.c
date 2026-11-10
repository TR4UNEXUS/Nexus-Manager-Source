// SPDX-License-Identifier: ISC
#include "switch.h"
#include "nexus_vsd.h"
#include "nexus_read.h"
#include "nexus/vsd_protocol.h"
#include "nexus/bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char target[]="/switch/nfsmw-nx/nfsmw-nx.nro";
static const u64 code_size=62943232; // actual user's NFS executable size
static int refs,handles,present=1,ready=1,version=1,cap=1,opens,reads,begins,arms;
static int fail_cmd=-1,short_read,oversize,changed,activation=1,sleeps;
static u64 generation=17;
void nexusTrace(const char* a,Result b,u64 c){(void)a;(void)b;(void)c;}
Result smInitialize(void){++refs;return 0;}void smExit(void){assert(refs>0);--refs;}
Result smGetService(Service* s,const char* n){assert(!strcmp(n,"ams:usb"));s->active=1;++handles;++opens;return 0;}
void serviceClose(Service* s){assert(s->active);s->active=0;--handles;}
SmServiceName smEncodeName(const char* n){assert(!strcmp(n,"ams:usb"));return 1;}
bool hosversionAtLeast(int a,int b,int c){(void)a;(void)b;(void)c;return true;}
void svcSleepThread(u64 ns){assert(ns==100000000);++sleeps;}
Result mockDispatch(int cmd,const void* in,size_t len,void* out,size_t outlen,Params p){
    if(cmd==fail_cmd)return 123;
    if(cmd==65100){assert(outlen==1);*(u8*)out=present;return 0;}
    if(cmd==0){assert(outlen==16);memset(out,0,16);u8* b=out;b[0]=b[1]=ready;b[2]=ready?4:1;((u64*)out)[1]=generation;return 0;}
    if(cmd==6){assert(len==8 && outlen==16 && *(const u64*)in==generation);assert(!strcmp(p.buffers[0].ptr,target));memset(out,0,16);*(u64*)out=code_size;return 0;}
    if(cmd==7){
        assert(len==16 && outlen==8 && p.buffer_attrs[1]==(SfBufferAttr_HipcMapAlias|SfBufferAttr_Out));
        const u64* io=in;assert(io[1]==17); // never silently adopt a reconnected medium
        assert(!strcmp(p.buffers[0].ptr,target));assert(p.buffers[0].size==sizeof(target));
        assert(p.buffers[1].size>0 && p.buffers[1].size<=NEXUS_NRO_READ_CHUNK);
        ++reads;if(changed && reads==2)return 456;
        assert(!arms && !begins); // executable I/O precedes any VSD mapping
        const size_t n=p.buffers[1].size;
        for(size_t i=0;i<n;++i)((u8*)p.buffers[1].ptr)[i]=(u8)((io[0]+i)*37);
        *(u64*)out=short_read?n-1:oversize?n+1:n;return 0;
    }
    assert(p.in_send_pid && in && ((const u64*)in)[0]==0);
    if(cmd==NEXUS_VSD_ARM){assert(len==16 && ((const u64*)in)[1]==17);assert(!strcmp(p.buffers[0].ptr,target));assert(begins==1);++arms;return 0;}
    assert(len==8 && outlen==sizeof(NexusVsdStatus));
    NexusVsdStatus* s=out;*s=(NexusVsdStatus){.magic=NEXUS_VSD_MAGIC,.version=version,.reserved=cap};
    if(cmd==NEXUS_VSD_BEGIN_LOAD){assert(p.in_num_handles==1 && p.in_handles[0]==42);++begins;
        assert(!strcmp(p.buffers[0].ptr,begins==1?"/":target));
        s->flags=arms&&activation?5:1;s->generation=17;
    }else assert(cmd==NEXUS_VSD_QUERY);
    return 0;
}
static void reset(void){assert(!refs && !handles);present=ready=version=cap=activation=1;generation=17;fail_cmd=-1;short_read=oversize=changed=sleeps=reads=opens=arms=begins=0;}
int main(void){
    assert(nexusDeferUsbRegistration(true,NEXUS_SD_APP_PATH,NULL));
    assert(!nexusDeferUsbRegistration(false,NEXUS_SD_APP_PATH,NULL));
    assert(!nexusDeferUsbRegistration(true,NEXUS_SD_APP_PATH,target));
    assert(!nexusDeferUsbRegistration(true,target,NULL));
    assert(!nexusDeferUsbRegistration(true,"/switch/usbloader/../USBLoader.nro",NULL));
    assert(nexusHasSdBootstrap(NEXUS_SD_BOOTSTRAP_INFO,sizeof(NEXUS_SD_BOOTSTRAP_INFO)));
    assert(!nexusHasSdBootstrap(NEXUS_SD_BOOTSTRAP_INFO,4));
    assert(!nexusHasSdBootstrap("USB Loader",11));
    NexusUsbSource s={0};u8* buffer=malloc(code_size);assert(buffer);
    assert(!nexusUsbOpen(&s,target));assert(!arms && !begins);
    assert(!nexusReadExact(nexusUsbRead,&s,0,buffer,code_size));
    assert(reads==(code_size+NEXUS_NRO_READ_CHUNK-1)/NEXUS_NRO_READ_CHUNK);
    for(u64 i=0;i<code_size;++i)assert(buffer[i]==(u8)(i*37));
    assert(!nexusUsbActivate(&s,42,target));assert(arms==1 && begins==2);
    nexusUsbClose(&s);assert(!s.generation && !s.size);nexusUsbClose(&s);
    for(int mode=0;mode<3;++mode){reset();assert(!nexusUsbOpen(&s,target));short_read=mode==0;oversize=mode==1;changed=mode==2;
        assert(nexusReadExact(nexusUsbRead,&s,0,buffer,code_size));assert(reads==(mode==2?2:1));assert(!arms && !begins);nexusUsbClose(&s);}
    for(int cmd=0;cmd<4;++cmd){reset();fail_cmd=(int[]){65100,NEXUS_VSD_QUERY,0,6}[cmd];assert(nexusUsbOpen(&s,target)==123);assert(!refs && !handles && !arms);}
    reset();present=0;assert(nexusUsbOpen(&s,target));assert(!opens && !refs);
    reset();ready=0;assert(nexusUsbOpen(&s,target));assert(sleeps==50 && !refs && !handles);
    reset();cap=0;assert(nexusUsbOpen(&s,target));assert(!refs && !handles);
    reset();version=9;assert(nexusUsbOpen(&s,target));assert(!refs && !handles);
    reset();assert(!nexusUsbOpen(&s,target));fail_cmd=NEXUS_VSD_ARM;assert(nexusUsbActivate(&s,42,target));assert(begins==1);nexusUsbClose(&s);
    reset();assert(!nexusUsbOpen(&s,target));activation=0;assert(nexusUsbActivate(&s,42,target));nexusUsbClose(&s);
    reset();assert(!nexusUsbOpen(&s,target));u64 actual=99;
    assert(nexusUsbRead(&s,code_size-1,buffer,2,&actual));assert(!actual && !reads);
    assert(nexusReadExact(nexusUsbRead,&s,UINT64_MAX-1,buffer,2));assert(!reads);
    nexusUsbClose(&s);assert(!refs && !handles);free(buffer);
    puts("Production USB reader: 63 MB bounded read, no pre-read activation, short/error/reconnect stop, protocol/cleanup and fresh-SD-only bootstrap passed");
}
