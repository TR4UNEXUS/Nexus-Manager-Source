// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
using Result=std::uint32_t;using u64=std::uint64_t;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
constexpr Result Fault=42;
struct ThreadState {void(*entry)(void*);void* arg;std::thread thread;};
struct Thread {ThreadState* state{};};
static std::atomic_uint created{},started{},joined{},closed{},queries{},pending_reads{},scans{},clients{},scan_buffers{},scan_entered{};
static bool fail_create{},fail_start{},fail_join{},fail_close{},applet{},available{true},pending{},scan_complete{true},block_scan{};
static Result query_error{},pending_error{},scan_error{};static u64 current_generation=7;
static unsigned cases{},assertions{};
#define CHECK(x) do{++assertions;assert(x);}while(0)
[[noreturn]]void fatalThrow(Result rc){throw rc;}
Result threadStart(Thread* thread){++started;if(fail_start)return Fault;thread->state->thread=std::thread([state=thread->state]{state->entry(state->arg);});return 0;}
Result threadWaitForExit(Thread* thread){++joined;if(fail_join)return Fault;thread->state->thread.join();return 0;}
Result threadClose(Thread* thread){++closed;if(fail_close)return Fault;assert(!thread->state->thread.joinable());delete thread->state;thread->state=nullptr;return 0;}
namespace nexus {
constexpr Result Invalid=5977;
namespace lifecycle {inline std::atomic_bool foreground{true};bool CanPoll(){return foreground.load();}}
struct Status {u64 generation{};bool mounted{};bool Available()const{return mounted;}};
struct Client {
    Client(){++clients;}~Client(){--clients;}
    Result Query(Status& status){++queries;status={current_generation,available};return query_error;}
};
}
namespace sphaira {
struct App {static bool IsApplet(){return applet;}};
namespace utils {
Result CreateThread(Thread* thread,void(*entry)(void*),void* arg,size_t stack){
    ++created;assert(stack>=128*1024);if(fail_create)return Fault;
    thread->state=new ThreadState{entry,arg,{}};return 0;
}
}
namespace nexusapp {
struct UsbLibraryStartupResult {Result result{};bool pending{};size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{};std::string detail;};
struct UsbImportScan {
    bool complete{};std::string detail;
    UsbImportScan(){++scan_buffers;}~UsbImportScan(){--scan_buffers;}
};
struct UsbLibrarySummary {size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{},selected_items{};};
static UsbLibrarySummary summary{3,2,1,1,7};
Result UsbImportPending(bool& out){++pending_reads;out=pending;return pending_error;}
Result ScanUsbImports(void* progress,UsbImportScan& scan,u64 generation,const std::atomic_bool* cancel,bool names){
    assert(!progress&&!names&&cancel&&generation==7);++scans;scan_entered=1;
    while(block_scan&&!cancel->load())std::this_thread::yield();
    scan.complete=scan_complete;scan.detail=scan_error?"Actual scan failure":std::string{};
    return cancel->load()?Fault:scan_error;
}
UsbLibrarySummary SummarizeUsbLibrary(const UsbImportScan& scan){assert(scan.complete);return summary;}
}
}
// PRODUCTION_WORKER
using namespace sphaira::nexusapp;
template<class Predicate>void Wait(Predicate ready){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!ready()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
void Reset(){
    CHECK(!startup_library.started&&clients==0&&scan_buffers==0);
    startup_library.~StartupLibraryJob();new(&startup_library)StartupLibraryJob{};
    created=started=joined=closed=queries=pending_reads=scans=scan_entered=0;
    fail_create=fail_start=fail_join=fail_close=applet=pending=block_scan=false;
    available=scan_complete=true;query_error=pending_error=scan_error=0;current_generation=7;
    nexus::lifecycle::foreground=true;summary={3,2,1,1,7};++cases;
}
UsbLibraryStartupResult Complete(){
    StartUsbLibraryStartupCheck();Wait([]{return startup_library.done.load();});
    CHECK(clients==0&&scan_buffers==0);UsbLibraryStartupResult result;CHECK(TakeUsbLibraryStartupCheck(result));
    CHECK(!startup_library.started&&joined==1&&closed==1);return result;
}
int main(){
    Reset();auto result=Complete();CHECK(result.affected_games==3&&result.repairable_games==2&&result.blocked_games==1&&result.missing_ticket_games==1);
    CHECK(scans==1&&queries==2&&pending_reads==1);CHECK(!TakeUsbLibraryStartupCheck(result));StartUsbLibraryStartupCheck();CHECK(created==1);
    Reset();summary={};result=Complete();CHECK(result.affected_games==0&&queries==1);
    Reset();available=false;result=Complete();CHECK(result.result==0&&!result.pending&&scans==0&&queries==1);
    Reset();query_error=Fault;result=Complete();CHECK(result.result==0&&scans==0);
    Reset();pending=true;available=false;result=Complete();CHECK(result.pending&&queries==0&&scans==0);
    Reset();pending_error=Fault;result=Complete();CHECK(result.result==Fault&&!result.detail.empty()&&scans==0);
    Reset();pending_error=Fault;available=false;result=Complete();CHECK(result.result==0&&!result.pending&&scans==0);
    Reset();scan_error=Fault;result=Complete();CHECK(result.result==Fault&&result.detail=="Actual scan failure"&&result.affected_games==0);
    Reset();scan_complete=false;result=Complete();CHECK(result.result==nexus::Invalid&&result.affected_games==0&&!result.detail.empty());
    for(bool replace:{false,true}){
        Reset();StartUsbLibraryStartupCheck();Wait([]{return startup_library.done.load();});
        if(replace)current_generation=8;else available=false;
        CHECK(TakeUsbLibraryStartupCheck(result)&&result.affected_games==0&&result.result==0);CHECK(joined==1&&closed==1);
    }
    Reset();block_scan=true;StartUsbLibraryStartupCheck();Wait([]{return scan_entered.load()!=0;});
    CHECK(!TakeUsbLibraryStartupCheck(result));StopUsbLibraryStartupCheck();
    CHECK(startup_library.cancel&&joined==1&&closed==1&&clients==0&&scan_buffers==0);CHECK(!TakeUsbLibraryStartupCheck(result));
    StartUsbLibraryStartupCheck();CHECK(created==1);StopUsbLibraryStartupCheck();CHECK(closed==1);
    Reset();StartUsbLibraryStartupCheck();Wait([]{return startup_library.done.load();});StopUsbLibraryStartupCheck();CHECK(!TakeUsbLibraryStartupCheck(result)&&startup_library.result.affected_games==0);
    Reset();StopUsbLibraryStartupCheck();StartUsbLibraryStartupCheck();CHECK(created==0&&pending_reads==0&&!TakeUsbLibraryStartupCheck(result));
    Reset();nexus::lifecycle::foreground=false;CancelUsbLibraryStartupCheck();nexus::lifecycle::foreground=true;
    result=Complete();CHECK(created==1&&result.affected_games==3);
    Reset();block_scan=true;StartUsbLibraryStartupCheck();Wait([]{return scan_entered.load()!=0;});nexus::lifecycle::foreground=false;CancelUsbLibraryStartupCheck();
    Wait([]{return startup_library.done.load();});CHECK(!TakeUsbLibraryStartupCheck(result)&&closed==1);nexus::lifecycle::foreground=true;CHECK(!TakeUsbLibraryStartupCheck(result));
    Reset();applet=true;StartUsbLibraryStartupCheck();CHECK(created==0&&TakeUsbLibraryStartupCheck(result)&&result.affected_games==0);
    Reset();fail_create=true;StartUsbLibraryStartupCheck();CHECK(created==1&&started==0&&closed==0&&TakeUsbLibraryStartupCheck(result));
    Reset();fail_start=true;StartUsbLibraryStartupCheck();CHECK(created==1&&started==1&&closed==1&&joined==0&&TakeUsbLibraryStartupCheck(result));
    Reset();StartUsbLibraryStartupCheck();Wait([]{return startup_library.done.load();});fail_join=true;
    try{StopUsbLibraryStartupCheck();CHECK(false);}catch(Result rc){CHECK(rc==Fault&&startup_library.started&&closed==0);}
    fail_join=false;StopUsbLibraryStartupCheck();CHECK(closed==1&&!startup_library.started);
    std::cout<<"USB startup worker: "<<cases<<" cases / "<<assertions<<" checks; actual threaded cancellation, service cleanup, delivery generation, quiet absence and foreground stop passed\n";
}
