// SPDX-License-Identifier: ISC
#include "nexus_vsd.h"
#include "nexus_trace.h"
#include "nexus_read.h"
#include "nexus/vsd_protocol.h"
#include <string.h>
static bool known_companion;
static Result Begin(Service* service,Handle process,const char* path,NexusVsdStatus* status){
    const u64 pid_placeholder=0;
    nexusTrace("vsd-begin-load-call",0,0);
    const Result rc=serviceDispatchInOut(service,NEXUS_VSD_BEGIN_LOAD,pid_placeholder,*status,
        .in_send_pid=true,.in_num_handles=1,.in_handles={process},
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},.buffers={{path,strlen(path)+1}});
    nexusTrace("vsd-begin-load-result",rc,status->flags);
    return rc;
}
Result nexusVsdBeforeLoad(Handle process,const char* path,const char* initial_usb){
    const bool required=initial_usb && initial_usb[0];
    const Result unavailable=MAKERESULT(Module_Libnx,LibnxError_NotFound);
    nexusTrace("vsd-sm-init-begin",0,0);
    Result rc=smInitialize();
    nexusTrace("vsd-sm-init-end",rc,0);
    if(R_FAILED(rc))return required||known_companion?rc:0;
    Service service={0};u8 present=0;const SmServiceName name=smEncodeName("ams:usb");
    nexusTrace("vsd-service-detect-begin",0,0);
    rc=hosversionAtLeast(12,0,0)?tipcDispatchInOut(smGetServiceSessionTipc(),65100,name,present):serviceDispatchInOut(smGetServiceSession(),65100,name,present);
    nexusTrace("vsd-service-detect-end",rc,present);
    if(R_FAILED(rc)||!present){smExit();return required||known_companion?(R_FAILED(rc)?rc:unavailable):0;}
    nexusTrace("vsd-service-open-begin",0,0);
    rc=smGetService(&service,"ams:usb");
    nexusTrace("vsd-service-open",rc,0);
    if(R_SUCCEEDED(rc)){
        const u64 pid_placeholder=0;NexusVsdStatus status={0};
        nexusTrace("vsd-query-begin",0,0);
        rc=serviceDispatchInOut(&service,NEXUS_VSD_QUERY,pid_placeholder,status,.in_send_pid=true);
        nexusTrace("vsd-query-result",rc,status.flags);
        if(R_SUCCEEDED(rc)&&status.magic==NEXUS_VSD_MAGIC&&status.version==NEXUS_VSD_VERSION){
            known_companion=true;
            if(required){
                // Register THIS HOME application's process handle, not the creator PID.
                rc=Begin(&service,process,"/",&status);
                struct {u8 mounted,ready,state,reserved;u32 result;u64 generation;} media={0};
                if(R_SUCCEEDED(rc)){
                    nexusTrace("usb-ready-wait-begin",0,0);
                    for(unsigned i=0;i<50;++i){
                        rc=serviceDispatchOut(&service,0,media);
                        if(R_FAILED(rc) || (media.mounted&&media.ready&&media.state==4&&!media.result&&media.generation))break;
                        svcSleepThread(100000000);
                    }
                    if(R_SUCCEEDED(rc) && !(media.mounted&&media.ready&&media.state==4&&!media.result&&media.generation))rc=unavailable;
                    nexusTrace("usb-ready-wait-end",rc,media.generation);
                    if(R_SUCCEEDED(rc) && !strncmp(initial_usb,"/switch/",8) && !(status.reserved&NEXUS_VSD_CAP_USB_ROOT))rc=unavailable;
                    if(R_SUCCEEDED(rc)){
                        const struct {u64 pid_placeholder,generation;} in={0,media.generation};
                        nexusTrace("vsd-arm-begin",0,media.generation);
                        rc=serviceDispatchIn(&service,NEXUS_VSD_ARM,in,.in_send_pid=true,
                            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},.buffers={{initial_usb,strlen(initial_usb)+1}});
                        nexusTrace("vsd-arm-end",rc,media.generation);
                    }
                }
                if(R_SUCCEEDED(rc)){
                    rc=Begin(&service,process,path,&status);
                    if(R_SUCCEEDED(rc) && (!(status.flags&NEXUS_VSD_ACTIVE)||status.generation!=media.generation))rc=unavailable;
                }
            }else rc=Begin(&service,process,path,&status);
        }else if(!known_companion && !required)rc=0;
        else if(R_SUCCEEDED(rc))rc=unavailable;
        serviceClose(&service);
    }else if(!known_companion && !required)rc=0;
    smExit();return rc;
}

void nexusUsbClose(NexusUsbSource* s) {
    if(s->service_open)serviceClose(&s->service);
    if(s->sm_open)smExit();
    memset(s,0,sizeof(*s));
}

Result nexusUsbOpen(NexusUsbSource* s,const char* physical) {
    const Result unavailable=MAKERESULT(Module_Libnx,LibnxError_NotFound);
    const Result invalid=MAKERESULT(Module_Libnx,LibnxError_BadInput);
    memset(s,0,sizeof(*s));
    if(!physical || physical[0]!='/')return invalid;
    size_t length=0;while(length<sizeof(s->physical) && physical[length])++length;
    if(length==sizeof(s->physical))return invalid;
    strcpy(s->physical,physical);
    Result rc=smInitialize();
    if(R_FAILED(rc))return rc;
    s->sm_open=true;
    u8 present=0;const SmServiceName name=smEncodeName("ams:usb");
    rc=hosversionAtLeast(12,0,0)?tipcDispatchInOut(smGetServiceSessionTipc(),65100,name,present):serviceDispatchInOut(smGetServiceSession(),65100,name,present);
    if(R_SUCCEEDED(rc) && !present)rc=unavailable;
    if(R_SUCCEEDED(rc)){rc=smGetService(&s->service,"ams:usb");s->service_open=R_SUCCEEDED(rc);}
    NexusVsdStatus status={0};const u64 pid_placeholder=0;
    if(R_SUCCEEDED(rc))rc=serviceDispatchInOut(&s->service,NEXUS_VSD_QUERY,pid_placeholder,status,.in_send_pid=true);
    if(R_SUCCEEDED(rc) && (status.magic!=NEXUS_VSD_MAGIC || status.version!=NEXUS_VSD_VERSION ||
        (!strncmp(physical,"/switch/",8) && !(status.reserved&NEXUS_VSD_CAP_USB_ROOT))))rc=unavailable;
    struct {u8 mounted,ready,state,reserved;u32 result;u64 generation;} media={0};
    if(R_SUCCEEDED(rc)){
        for(unsigned i=0;i<50;++i){
            rc=serviceDispatchOut(&s->service,0,media);
            if(R_FAILED(rc) || (media.mounted&&media.ready&&media.state==4&&!media.result&&media.generation))break;
            svcSleepThread(100000000);
        }
        if(R_SUCCEEDED(rc) && !(media.mounted&&media.ready&&media.state==4&&!media.result&&media.generation))rc=unavailable;
    }
    struct {u64 size;u32 cluster;u8 directory,padding[3];} info={0};
    if(R_SUCCEEDED(rc)){
        s->generation=media.generation;
        rc=serviceDispatchInOut(&s->service,6,s->generation,info,
            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
            .buffers={{s->physical,strlen(s->physical)+1}});
        if(R_SUCCEEDED(rc) && (info.directory || info.size<0x80))rc=invalid;
        s->size=info.size;
    }
    nexusTrace("usb-source-open",rc,s->generation);
    if(R_FAILED(rc))nexusUsbClose(s);
    return rc;
}

Result nexusUsbRead(void* context,u64 offset,void* buffer,size_t size,u64* actual) {
    NexusUsbSource* s=context;*actual=0;
    if(!s->service_open || !s->generation || !buffer || !size || size>NEXUS_NRO_READ_CHUNK ||
        offset>s->size || size>s->size-offset)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    const struct {u64 offset,generation;} in={offset,s->generation};
    return serviceDispatchInOut(&s->service,7,in,*actual,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In,SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
        .buffers={{s->physical,strlen(s->physical)+1},{buffer,size}});
}

Result nexusUsbActivate(NexusUsbSource* s,Handle process,const char* logical) {
    if(!s->service_open || !s->generation || !process || !logical)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    NexusVsdStatus status={0};
    Result rc=Begin(&s->service,process,"/",&status);
    if(R_SUCCEEDED(rc)){
        known_companion=true;
        const struct {u64 pid_placeholder,generation;} in={0,s->generation};
        rc=serviceDispatchIn(&s->service,NEXUS_VSD_ARM,in,.in_send_pid=true,
            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
            .buffers={{s->physical,strlen(s->physical)+1}});
    }
    if(R_SUCCEEDED(rc))rc=Begin(&s->service,process,logical,&status);
    if(R_SUCCEEDED(rc) && (!(status.flags&NEXUS_VSD_ACTIVE) || status.generation!=s->generation))rc=MAKERESULT(Module_Libnx,LibnxError_NotFound);
    nexusTrace("usb-source-activate",rc,s->generation);
    return rc;
}
