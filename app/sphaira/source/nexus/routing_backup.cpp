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
    R_UNLESS(nexus::registration::Compatible(c)&&(c.features&4),nexus::registration::FirmwareUpdateRequired);return 0;
}
}
Result QueryConsoleRoutingBackup(nexus::routing_backup::Status&status){
    status={};R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());R_TRY(Supported());
    R_TRY(serviceDispatch(ncmGetServiceSession(),65034,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&status,sizeof(status)}}));
    R_UNLESS(nexus::routing_backup::Valid(status),nexus::Invalid);return 0;
}
Result QueryConsoleRoutingBackupDiagnostic(nexus::routing_backup::Diagnostic&d){
    d={};R_TRY(ncmInitialize());ON_SCOPE_EXIT(ncmExit());
    nexus::registration::Capabilities caps{};
    R_TRY(serviceDispatch(ncmGetServiceSession(),65033,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&caps,sizeof(caps)}}));
    R_UNLESS(nexus::registration::Compatible(caps)&&(caps.features&8),nexus::Unavailable);
    R_TRY(serviceDispatch(ncmGetServiceSession(),65037,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&d,sizeof(d)}}));
    R_UNLESS(nexus::routing_backup::Valid(d),nexus::Invalid);
    nexus::Log("backup-failure-stage",d.result,d.stage);
    nexus::Log("backup-map-index",0,d.index);nexus::Log("backup-expected-bytes",0,d.expected);
    nexus::Log("backup-actual-bytes",0,d.actual);nexus::Log("backup-attempts",0,d.attempts);
    return 0;
}
std::string ConsoleRoutingBackupReason(Result expected_result){
    nexus::routing_backup::Diagnostic d{};
    if(R_FAILED(QueryConsoleRoutingBackupDiagnostic(d))||!d.result||(expected_result&&expected_result!=d.result))return {};
    switch(d.stage){
    case 1:case 13:
        return d.result==0x003fe805 ? "A USB repair or import is unfinished. Finish recovery before saving the library backup."_nx :
            "The app could not check for an unfinished USB repair. Keep the SD card connected and try again."_nx;
    case 2:case 14:return "A routing file could not be read. Use Repair USB games to check the current mappings."_nx;
    case 3:case 4:case 15:return "A routing file is incomplete or could not be verified. Existing backups were kept."_nx;
    case 5:return "The console backup save could not be opened or its ownership could not be confirmed. It was not erased."_nx;
    case 6:case 7:return "The saved backup archive needs attention. Any recoverable or rejected copy was kept."_nx;
    case 10:return "The USB library changed while saving. Wait for transfers to finish, then save again."_nx;
    default:return "The console backup could not finish writing or verification. Any previous valid backup was kept."_nx;
    }
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
