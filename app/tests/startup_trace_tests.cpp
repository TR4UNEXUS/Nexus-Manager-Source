#define NEXUS_MOCK_IMPLEMENTATION
#include "nexus/startup_trace.hpp"
#include "nexus/trace_ring.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <cstring>
#include <array>
static bool fail_open, fail_create, fail_file_open, fail_write, busy, fail_exit;
static int opens, closes, file_closes, writes, operations;
static u64 tick;
static std::array<std::string,2> files;
static std::array<bool,2> active{};
bool mutexTryLock(Mutex* m) { if (busy || *m) return false; *m = 1; return true; }
void mutexUnlock(Mutex* m) { assert(*m); *m = 0; }
u64 svcGetSystemTick() { return ++tick; }
Result svcGetThreadId(u64* out, Handle h) { assert(h == CUR_THREAD_HANDLE); *out = 9; return 0; }
Result svcGetInfo(u64* out, int, Handle, u64) { *out = 0x05005553424c0000; return 0; }
Result fsOpenSdCardFileSystem(FsFileSystem* s) { ++opens; s->id = 7; return fail_open; }
Result fsFsCreateDirectory(FsFileSystem* s, const char*) { assert(s->id == 7); return 0; }
Result fsFsCreateFile(FsFileSystem* s, const char* p, u64 size, u32 opt) {
    assert(s->id == 7 && size == 0 && opt == 0);
    assert(std::strstr(p, "/switch/nexusmanager/startup-debug/app-05005553424c0000-"));
    return fail_create;
}
Result fsFsOpenFile(FsFileSystem* s, const char* p, u32 mode, FsFile* f) {
    assert(s->id == 7 && mode == (FsOpenMode_Write | FsOpenMode_Append));
    f->id = std::strstr(p,"-exit.log") ? 9 : 8;
    active[f->id-8] = !fail_file_open;
    return fail_file_open;
}
Result fsFileWrite(FsFile* f, u64 off, const void* buf, u64 len, u32 mode) {
    const auto id = f->id-8;
    assert(id>=0 && id<2 && active[id]);
    assert(len && len%320==0 && len<=16*320 && off%320==0 && off+len<=u64(id?102:819)*320 && mode==FsWriteOption_Flush);
    ++writes; if (fail_write || (fail_exit && id==1)) return 1;
    auto& text = files[id];
    assert(off<=text.size());
    text.resize(std::max(text.size(),size_t(off+len)));
    text.replace(off,len,static_cast<const char*>(buf),len);
    assert(text[off+len-1]=='\n');
    return 0;
}
void fsFileClose(FsFile* f) { assert(active.at(f->id-8)); active[f->id-8]=false; ++file_closes; }
void fsFsClose(FsFileSystem* s) { assert(s->id == 7 && !active[0] && !active[1]); ++closes; }
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "normal";
    fail_open = mode == "open-fail"; fail_create = mode == "create-fail";
    fail_file_open = mode == "file-open-fail"; fail_exit = mode == "exit-write-fail";
    nexus::startup::Open();
    fail_write = mode == "write-fail";
    const auto result = nexus::startup::Call("unchanged-operation", []{ ++operations; return Result{0x1234}; }, 5);
    assert(result == 0x1234 && operations == 1);
    busy = true; const int before = writes; nexus::startup::Event("busy"); assert(writes == before);
    busy = false; nexus::startup::Event("after-busy");
    if (mode == "normal" || fail_exit) {
        assert(files[0].find("phase=end rc=00001234") != std::string::npos);
        assert(files[0].find("skipped=1") != std::string::npos);
        for (int i = 0; i < 10000; ++i) nexus::startup::Event("bounded", "point", 0, i);
        assert(files[0].size()==819*320);
        assert(files[0].find("value=000000000000270f")!=std::string::npos);
        assert(files[0].find("seq=1 ")==std::string::npos);
    }
    const auto runtime = files[0];
    nexus::startup::BeginShutdown();
    for (int i=0;i<300;++i) nexus::startup::Event("cleanup", "point", 0, i);
    nexus::startup::CallVoid("cleanup-void", []{++operations;});
    assert(operations==2);
    nexus::startup::Event("exit-unlock-ready");
    nexus::startup::Close();
    if(mode=="normal") {
        assert(files[0]==runtime && files[1].size()==102*320);
        assert(files[1].find("stage=exit-unlock-ready")!=std::string::npos);
        assert(files[1].find("stage=trace-close")!=std::string::npos);
        assert(files[1].find("MANAGER-0.1.11-ports2")!=std::string::npos);
    }
    if(fail_exit) assert(files[0].find("stage=exit-unlock-ready")!=std::string::npos);
    const auto saved = files;
    nexus::startup::Open(); nexus::startup::Event("no-reopen"); nexus::startup::Close();
    assert(opens == 1 && files == saved);
    const u64 failure = nexus::startup::FailureInfo();
    const unsigned expected_stage = fail_open ? 1 : fail_create ? 2 : fail_file_open ? 3 : (fail_write||fail_exit) ? 4 : 0;
    assert((failure >> 32) == expected_stage);
    assert(static_cast<Result>(failure) == (expected_stage ? 1u : 0u));
    if (fail_open) assert(closes == 0 && file_closes == 0 && writes == 0);
    else if (fail_create || fail_file_open) assert(closes == 1 && file_closes == 0 && writes == 0);
    else assert(closes == 1 && file_closes == 2 && writes > 0);
    std::puts("PASS actual startup/exit logger: bounded ring, result preservation, independent exit log, cleanup and failures");
}
