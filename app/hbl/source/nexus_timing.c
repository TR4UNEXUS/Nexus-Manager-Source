// SPDX-License-Identifier: ISC
#include "nexus_timing.h"
#include <string.h>

static bool enabled;
static u64 entered;
static NexusStartupLoader record;

void nexusTimingEnter(void){entered=svcGetSystemTick();}
void nexusTimingConfigure(bool active,bool usb,const char* path){
    enabled=active;memset(&record,0,sizeof(record));
    if(!enabled)return;
    record.magic=NEXUS_STARTUP_MAGIC;record.version=NEXUS_STARTUP_VERSION;
    record.source=usb?NEXUS_STARTUP_SOURCE_USB:NEXUS_STARTUP_SOURCE_SD;
    if(path && !strncmp(path,"sdmc:",5))path+=5;
    if(path && strlen(path)<sizeof(record.path))strcpy(record.path,path);
}
u64 nexusTimingBegin(void){return enabled?svcGetSystemTick():0;}
void nexusTimingEnd(enum NexusStartupStage stage,u64 started){
    if(enabled && (unsigned)stage<NEXUS_STARTUP_STAGE_COUNT)
        record.duration_ns[stage]+=armTicksToNs(svcGetSystemTick()-started);
}
void nexusTimingBytes(u64 bytes){if(enabled)record.nro_bytes=bytes;}
void nexusTimingHash(const u8 hash[32],bool matched){
    if(!enabled)return;
    record.flags|=NEXUS_STARTUP_HASH_PERFORMED;
    if(matched)record.flags|=NEXUS_STARTUP_HASH_MATCHED;
    memcpy(record.executable_sha256,hash,sizeof(record.executable_sha256));
}
Result nexusTimingSubmit(bool handoff,Result result){
    if(!enabled)return 0;
    if(record.path[0]!='/')return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    record.result=result;
    if(handoff)record.flags|=NEXUS_STARTUP_HANDOFF;
    record.duration_ns[NexusStartup_Total]=armTicksToNs(svcGetSystemTick()-entered);
    Result rc=smInitialize();if(R_FAILED(rc))return rc;
    Service service={0};u8 present=0;const SmServiceName name=smEncodeName("ams:usb");
    rc=hosversionAtLeast(12,0,0)?tipcDispatchInOut(smGetServiceSessionTipc(),65100,name,present):serviceDispatchInOut(smGetServiceSession(),65100,name,present);
    if(R_SUCCEEDED(rc) && !present)rc=MAKERESULT(Module_Libnx,LibnxError_NotFound);
    if(R_SUCCEEDED(rc))rc=smGetService(&service,"ams:usb");
    if(R_SUCCEEDED(rc)){
        const u64 pid_placeholder=0;u64 sequence=0;
        rc=serviceDispatchInOut(&service,NEXUS_STARTUP_SUBMIT,pid_placeholder,sequence,
            .in_send_pid=true,
            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
            .buffers={{&record,sizeof(record)}});
        if(R_SUCCEEDED(rc) && !sequence)rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);
        serviceClose(&service);
    }
    smExit();return rc;
}
