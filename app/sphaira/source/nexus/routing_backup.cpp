#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/routing_backup.hpp"
#include "nexus/nx_Client.hpp"
#include "ui/progress_box.hpp"
#include "defines.hpp"
#include <cstdio>
namespace sphaira::nexusapp {
namespace {
Result Supported(){
    nexus::registration::Capabilities c{};R_TRY(serviceDispatch(ncmGetServiceSession(),65033,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&c,sizeof(c)}}));
    R_UNLESS(nexus::registration::Compatible(c)&&(c.features&4),nexus::Invalid);return 0;
}
}
Result QueryConsoleRoutingBackup(nexus::routing_backup::Status&status){
    status={};R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());R_TRY(Supported());
    R_TRY(serviceDispatch(ncmGetServiceSession(),65034,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&status,sizeof(status)}}));
    R_UNLESS(nexus::routing_backup::Valid(status),nexus::Invalid);return 0;
}
Result CheckpointConsoleRoutingBackup(){
    R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());R_TRY(Supported());
    const auto rc=serviceDispatch(ncmGetServiceSession(),65035);nexus::Log("console-routing-backup-N4",rc);return rc;
}
Result ReadConsoleRoutingBackup(ui::ProgressBox*p,nexus::repair::Snapshot&out,nexus::routing_backup::Status&status){
    R_TRY(QueryConsoleRoutingBackup(status));R_UNLESS(status.flags&1,nexus::Invalid);
    R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());nexus::recovery::Bytes bytes(status.bytes);
    for(u64 offset=0;offset<status.bytes;){
        if(p)R_TRY(p->ShouldExitResult());const auto count=std::min<u64>(0x10000,status.bytes-offset);
        struct {u64 sequence,offset;} input{status.sequence,offset};
        R_TRY(serviceDispatchIn(ncmGetServiceSession(),65036,input,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{bytes.data()+offset,count}}));offset+=count;
        if(p)p->SetOverallTransfer(1000*offset/status.bytes,10000,"Reading the console's USB library backup"_nx);
    }
    R_UNLESS(nexus::routing_backup::Decode(bytes,status.sequence,out,[](const void*data,size_t size){std::array<u8,32> h{};sha256CalculateHash(h.data(),data,size);return h;}),nexus::Invalid);return 0;
}
Result ConfirmConsoleRoutingBackup(u64& sequence){
    sequence=0;FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
    auto snapshot=[&](nexus::repair::Snapshot& out)->Result{
        for(unsigned i=0;i<3;++i){
            char path[FS_MAX_PATH]{};snprintf(path,sizeof(path),"%s",nexus::repair::Live(i).c_str());
            FsFile file{};const auto rc=fsFsOpenFile(&sd,path,FsOpenMode_Read,&file);
            if(rc==MAKERESULT(2,1))continue;R_TRY(rc);ON_SCOPE_EXIT(fsFileClose(&file));
            s64 size{};R_TRY(fsFileGetSize(&file,&size));R_UNLESS(size>=0 && size<=s64(nexus::repair::MaxFile),nexus::Invalid);
            out[i].exists=true;out[i].data.resize(size);u64 got{};
            R_TRY(fsFileRead(&file,0,out[i].data.data(),size,FsReadOption_None,&got));R_UNLESS(got==u64(size),nexus::Invalid);
        }return 0;
    };
    nexus::repair::Snapshot before,archived,after;
    R_TRY(snapshot(before));R_TRY(CheckpointConsoleRoutingBackup());
    nexus::routing_backup::Status status{};R_TRY(ReadConsoleRoutingBackup(nullptr,archived,status));
    R_TRY(snapshot(after));R_UNLESS(before==after && archived==after,nexus::Invalid);
    sequence=status.sequence;nexus::Log("LR1-console-backup-confirmed",0,sequence);return 0;
}
Result MissingRoutingWithConsoleBackup(bool&available){
    available=false;FsFileSystem sd{};R_TRY(fsOpenSdCardFileSystem(&sd));ON_SCOPE_EXIT(fsFsClose(&sd));
    for(unsigned i=0;i<3;++i){char path[FS_MAX_PATH]{};snprintf(path,sizeof(path),"%s",nexus::repair::Live(i).c_str());FsDirEntryType type{};const auto r=fsFsGetEntryType(&sd,path,&type);
        if(R_SUCCEEDED(r))return 0;if(r!=MAKERESULT(2,1))return r;}
    nexus::routing_backup::Status status{};const auto r=QueryConsoleRoutingBackup(status);
    // Older firmware has no backup command. The existing library scan still
    // supplies its normal diagnosis; never turn this optional probe into failure.
    if(R_SUCCEEDED(r))available=(status.flags&1)!=0;return 0;
}
}
