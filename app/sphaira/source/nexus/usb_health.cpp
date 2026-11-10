// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_health.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/bootstrap.h"
#include <cstdio>
namespace sphaira::nexusapp {
namespace {
struct HealthIo {
    nexus::Client usb;
    FsFileSystem sd{};
    bool sd_open{};
    u64 generation{};
    ui::ProgressBox* progress;
    explicit HealthIo(ui::ProgressBox* p):progress(p){}
    ~HealthIo(){if(sd_open)fsFsClose(&sd);}
    Result Check(){R_TRY(progress->ShouldExitResult());return usb.Check(generation);}
    Result Probe(){
        R_TRY(progress->ShouldExitResult());
        nexus::Status status{};R_TRY(usb.Query(status));
        R_UNLESS(status.Available(),nexus::Unavailable);generation=status.generation;
        // Match recovery's guard: never inspect a USB-virtualized sdmc as SD.
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            NexusVsdStatus vs{};R_TRY(usb.VirtualSdStatus(vs));
            R_UNLESS(!(vs.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Invalid);
        }
        R_TRY(fsOpenSdCardFileSystem(&sd));sd_open=true;return Check();
    }
    Result Registered(){
        R_TRY(Check());nexus::Info info{};R_TRY(usb.Stat(generation,"/registered",info));
        R_UNLESS(info.directory,nexus::Invalid);return 0;
    }
    Result Pending(bool& exists){
        R_TRY(Check());char path[FS_MAX_PATH]{};
        snprintf(path,sizeof(path),"%s",nexus::repair::Pending);
        FsDirEntryType type{};auto rc=fsFsGetEntryType(&sd,path,&type);
        exists=R_SUCCEEDED(rc);return rc==MAKERESULT(2,1)?0:rc;
    }
    Result Read(size_t index,nexus::repair::Image& image){
        R_TRY(Check());image={};char path[FS_MAX_PATH]{};
        snprintf(path,sizeof(path),"%s",nexus::repair::Live(index).c_str());
        FsFile file{};auto rc=fsFsOpenFile(&sd,path,FsOpenMode_Read,&file);
        if(rc==MAKERESULT(2,1))return 0;
        R_TRY(rc);s64 size{};u64 actual{};rc=fsFileGetSize(&file,&size);
        if(R_SUCCEEDED(rc) && (size<0 || size>static_cast<s64>(nexus::repair::MaxFile)))rc=nexus::Invalid;
        if(R_SUCCEEDED(rc)){
            image.data.resize(size);
            if(size)rc=fsFileRead(&file,0,image.data.data(),image.data.size(),FsReadOption_None,&actual);
        }
        fsFileClose(&file);R_TRY(rc);R_UNLESS(actual==image.data.size(),nexus::Invalid);
        image.exists=true;return 0;
    }
};
}
Result CheckUsbHealth(ui::ProgressBox* progress,nexus::health::Report& report){
    HealthIo io(progress);return nexus::health::Run(io,report);
}
}
