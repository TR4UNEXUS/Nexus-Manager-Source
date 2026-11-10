// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/startup_trace.hpp"
#include "nexus/trace_ring.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace nexus::startup {
namespace {
FsFileSystem fs{};
FsFile file{}, exit_file{};
Mutex mutex{};
bool attempted{}, fs_open{}, file_open{}, exit_open{};
std::atomic_bool shutdown{};
TraceRing runtime_ring{0, 819}, exit_ring{0, 102}; // <256 KiB + <32 KiB per run
// Only frequent successful USB bookkeeping is batched. Other begin/end events,
// errors and shutdown remain synchronous. No logger thread or detached I/O.
constexpr size_t BatchRecords=16;
char pending[BatchRecords*TraceRing::RecordBytes]{};
size_t pending_count{};
u64 sequence{};
std::atomic<u32> skipped{};
std::atomic<u64> failure{};
enum FailureStage : u32 { SdOpen=1, CreateLog, OpenLog, WriteLog };
void Fail(FailureStage stage, Result rc) {
    u64 expected = 0;
    failure.compare_exchange_strong(expected, (u64(stage) << 32) | rc);
}
void CloseLocked() {
    if (file_open) { fsFileClose(&file); file_open = false; }
    if (exit_open) { fsFileClose(&exit_file); exit_open = false; }
    if (fs_open) { fsFsClose(&fs); fs_open = false; }
}
bool WriteRecords(FsFile& output,TraceRing& ring,const char* bytes,size_t count,bool final){
    while(count){
        const auto run=std::min(count,ring.capacity-ring.slot);
        const auto rc=fsFileWrite(&output,ring.Offset(),bytes,run*TraceRing::RecordBytes,FsWriteOption_Flush);
        if(R_FAILED(rc)){
            Fail(WriteLog,rc);fsFileClose(&output);
            if(final)exit_open=false;else file_open=false;
            return false;
        }
        for(size_t i=0;i<run;++i)ring.Advance();
        bytes+=run*TraceRing::RecordBytes;count-=run;
    }
    return true;
}
void FlushRoutineLocked(){
    if(pending_count && file_open)WriteRecords(file,runtime_ring,pending,pending_count,false);
    pending_count=0;
}
bool Routine(const char* stage){
    return !std::strcmp(stage,"usb-status-query") || !std::strcmp(stage,"usb-service-detect") ||
        !std::strcmp(stage,"usb-service-open") || !std::strcmp(stage,"usb-vsd-query");
}
void WriteLocked(const char* stage, const char* phase, Result rc, u64 value) {
    if(shutdown.load())FlushRoutineLocked();
    const bool final = shutdown.load() && exit_open;
    if (!final && !file_open) return;
    auto& ring = final ? exit_ring : runtime_ring;
    auto& output = final ? exit_file : file;
    u64 thread = 0;
    svcGetThreadId(&thread, CUR_THREAD_HANDLE);
    char line[TraceRing::RecordBytes];
    const int size = std::snprintf(line, sizeof(line),
        "MANAGER-0.1.11-ports2 seq=%llu tick=%016llx tid=%016llx stage=%s phase=%s rc=%08x value=%016llx skipped=%u",
        static_cast<unsigned long long>(++sequence),
        static_cast<unsigned long long>(svcGetSystemTick()),
        static_cast<unsigned long long>(thread), stage, phase, static_cast<unsigned>(rc),
        static_cast<unsigned long long>(value), skipped.exchange(0));
    if (size <= 0 || static_cast<size_t>(size) >= sizeof(line)-1) { ++skipped; return; }
    // A full fixed-width replacement cannot leave a stale suffix after wrap.
    std::memset(line + size, ' ', sizeof(line)-size);
    line[sizeof(line)-1] = '\n';
    if(!shutdown.load() && !rc && Routine(stage)){
        std::memcpy(pending+pending_count++*TraceRing::RecordBytes,line,sizeof(line));
        if(pending_count==BatchRecords)FlushRoutineLocked();
    }else{
        FlushRoutineLocked();
        // A failed buffered write may have closed the runtime log. It must
        // never change the service result or prevent the independent exit log.
        if(final?exit_open:file_open)WriteRecords(output,ring,line,1,final);
    }
}
bool Create(FsFile& output, const char (&name)[FS_MAX_PATH]) {
    // libnx sends FS_MAX_PATH bytes, so keep a concrete full-sized IPC object.
    char path[FS_MAX_PATH]{};
    std::memcpy(path, name, sizeof(path));
    auto rc = fsFsCreateFile(&fs, path, 0, 0);
    if (R_FAILED(rc)) { Fail(CreateLog, rc); return false; }
    rc = fsFsOpenFile(&fs, path, FsOpenMode_Write | FsOpenMode_Append, &output);
    if (R_FAILED(rc)) { Fail(OpenLog, rc); return false; }
    return true;
}
}
void Open() {
    if (attempted) return;
    attempted = true;
    const auto rc = fsOpenSdCardFileSystem(&fs);
    if (R_FAILED(rc)) { Fail(SdOpen, rc); return; }
    fs_open = true;
    static const char directories[][FS_MAX_PATH] = {
        "/switch", "/switch/nexusmanager", "/switch/nexusmanager/startup-debug"
    };
    for (const auto& dir : directories) fsFsCreateDirectory(&fs, dir);
    u64 title = 0;
    svcGetInfo(&title, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    const auto run = svcGetSystemTick();
    char path[FS_MAX_PATH]{};
    std::snprintf(path, sizeof(path), "/switch/nexusmanager/startup-debug/app-%016llx-%016llx.log",
        static_cast<unsigned long long>(title), static_cast<unsigned long long>(run));
    file_open = Create(file, path);
    std::snprintf(path, sizeof(path), "/switch/nexusmanager/startup-debug/app-%016llx-%016llx-exit.log",
        static_cast<unsigned long long>(title), static_cast<unsigned long long>(run));
    // Reserve/open now: shutdown requires no new directory/file/service open.
    exit_open = Create(exit_file, path);
    Event("user-app-init-entered", "point", 0, title);
}
u64 FailureInfo() { return failure.load(); }
void BeginShutdown() {
    if (shutdown.exchange(true)) return;
    Event("shutdown-log", "begin");
}
void Event(const char* stage, const char* phase, Result rc, u64 value) {
    if (!mutexTryLock(&mutex)) { ++skipped; return; }
    WriteLocked(stage, phase, rc, value);
    mutexUnlock(&mutex);
}
void Close() {
    // All app workers must be joined first; no IPC on any other logger thread.
    if (!mutexTryLock(&mutex)) { ++skipped; return; }
    WriteLocked("trace-close", "point", 0, 0);
    CloseLocked();
    mutexUnlock(&mutex);
}
}
