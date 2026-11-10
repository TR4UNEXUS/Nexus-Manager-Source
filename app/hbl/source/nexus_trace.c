// SPDX-License-Identifier: ISC
#include "nexus_trace.h"
#include <stdio.h>
#include <string.h>

// This filesystem/file is opened BEFORE activating Virtual SD. Never reopen it
// once the process has a USB view, and close it before handing control to an NRO.
// No keys, memory dumps, tokens, or file contents are recorded.
static FsFileSystem trace_fs;
static FsFile trace_file;
static bool open_attempted, configured, disabled, fs_open, file_open;
static u64 trace_offset;
static char pending[16384];
static size_t pending_size;

void nexusTraceClose(void) {
    disabled=true;pending_size=0;
    if(file_open){fsFileClose(&trace_file);file_open=false;}
    if(fs_open){fsFsClose(&trace_fs);fs_open=false;}
}

void nexusTrace(const char* stage, Result result, u64 value) {
    if(disabled || trace_offset>=16384)return;
    char line[192];
    const int size=snprintf(line,sizeof(line),"HBL-0.6.8-SD8 tick=%016llx stage=%s rc=%08x value=%016llx\n",
        (unsigned long long)svcGetSystemTick(),stage,(unsigned)result,(unsigned long long)value);
    if(size<=0 || (size_t)size>=sizeof(line) || trace_offset+(u64)size>16384)return;
    if(!configured){
        if(pending_size+(size_t)size<=sizeof(pending)){
            memcpy(pending+pending_size,line,size);pending_size+=size;
        }
        return;
    }
    if(!file_open)return;
    if(R_FAILED(fsFileWrite(&trace_file,trace_offset,line,size,FsWriteOption_Flush))){nexusTraceClose();return;}
    trace_offset+=size;
}

void nexusTraceOpen(void) {
    if(open_attempted)return; // chainloads must not mistake the USB view for SD
    open_attempted=true;
    u64 title=0;
    svcGetInfo(&title,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    nexusTrace("loader-entered",0,title);
}

void nexusTraceConfigure(bool file_logging) {
    if(configured || disabled)return;
    configured=true;
    if(!file_logging){disabled=true;pending_size=0;return;}
    // Normal launches retain their early timestamps. Timed launches never open
    // the SD at all. Failures before RomFS configuration leave only RAM traces.
    if(R_FAILED(fsOpenSdCardFileSystem(&trace_fs)))return;
    fs_open=true;
    // These directories already exist in a normal install; errors are handled
    // by the subsequent exclusive create, with no overwrite of any user file.
    // Every libnx path request sends FS_MAX_PATH bytes, not strlen(path).
    static const char directories[][FS_MAX_PATH]={
        "/switch", "/switch/nexusmanager", "/switch/nexusmanager/forwarder-debug"
    };
    fsFsCreateDirectory(&trace_fs,directories[0]);
    fsFsCreateDirectory(&trace_fs,directories[1]);
    fsFsCreateDirectory(&trace_fs,directories[2]);
    u64 title=0;
    svcGetInfo(&title,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    char path[FS_MAX_PATH]={0};
    snprintf(path,sizeof(path),"/switch/nexusmanager/forwarder-debug/%016llx-%016llx.log",
        (unsigned long long)title,(unsigned long long)svcGetSystemTick());
    if(R_FAILED(fsFsCreateFile(&trace_fs,path,0,0)) ||
       R_FAILED(fsFsOpenFile(&trace_fs,path,FsOpenMode_Write|FsOpenMode_Append,&trace_file))){nexusTraceClose();return;}
    file_open=true;
    if(pending_size){
        if(R_FAILED(fsFileWrite(&trace_file,0,pending,pending_size,FsWriteOption_Flush))){nexusTraceClose();return;}
        trace_offset=pending_size;pending_size=0;
    }
}
