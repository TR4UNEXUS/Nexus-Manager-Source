// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <algorithm>
#include <set>
#include <vector>
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
constexpr Result Missing = 7;
#define MAKERESULT(m,n) Missing
void svcSleepThread(long long){std::this_thread::yield();}
static bool compatible=true, marker=true, partial=false, backup=false, new_work=true, blocked=false;
static bool removed=false,staging=false,replacing=false;
namespace nexus {
void Log(const char*,Result){}
namespace hats { bool SupportsPcLibrary(){return compatible;} }
namespace lifecycle {inline std::atomic_bool foreground{true};bool CanPoll(){return foreground.load();}}
struct Status {u64 generation{};bool mounted{};bool Available()const{return mounted;}};
struct Info {bool directory{};u64 size{};};
struct Client {
    Client(){++clients;}~Client(){--clients;}
    Result Query(Status& status){++queries;status={current_generation,available};return query_error;}
    Result Check(u64 gen){return available && current_generation==gen ? 0 : Fault;}
    Result Stat(u64,const std::string& path,Info& out){
        out={};if(path.ends_with("/session.lock"))return marker?0:Missing;
        if(path.ends_with("/install-stage")||path.ends_with("/install-ready"))return staging?0:Missing;
        if(path.ends_with("/replace-stage")||path.ends_with("/replace-ready"))return replacing?0:Missing;
        assert(path.ends_with("/content.partial"));return partial?0:Missing;
    }
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
struct PcRemoval {u64 generation;};
Result ListPcRemovals(void*,std::vector<PcRemoval>& out,const std::atomic_bool*){
    if(removed)out.push_back({current_generation});
    return 0;
}
struct Row {struct {u64 application_id;} record;bool needs_home_import;bool selected;};
struct UsbImportScan {u64 generation{};bool complete{};std::vector<Row> titles;};
Result UsbImportPending(bool& out){++pending_reads;out=pending;return pending_error;}
Result MissingRoutingWithConsoleBackup(bool& out){out=backup;return 0;}
struct Plan {std::vector<bool> selected;};
Plan PlanUsbLibrary(const UsbImportScan& scan){Plan p;for(auto& row:scan.titles)p.selected.push_back(row.selected);return p;}
Result ScanUsbImports(void* progress,UsbImportScan& scan,u64 gen,const std::atomic_bool* cancel,bool names){
    assert(!progress && !names && cancel);++scans;scan_entered=1;
    while(block_scan&&!cancel->load())std::this_thread::yield();
    scan.complete=scan_complete;scan.generation=gen;
    scan.titles={{{1},new_work,!blocked},{{1},false,!blocked},{{2},false,true},{{3},true,false}};
    return cancel->load()?Fault:scan_error;
}
}
}
// PRODUCTION_WORKER
using namespace sphaira::nexusapp;
template<class Predicate>void Wait(Predicate ready){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!ready()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
void Reset(){
    CHECK(!detection.started&&clients==0);
    detection.~Detection();new(&detection)Detection{};
    created=started=joined=closed=queries=pending_reads=scans=scan_entered=0;
    fail_create=fail_start=fail_join=fail_close=applet=pending=block_scan=partial=backup=blocked=false;
    available=scan_complete=compatible=marker=new_work=true;query_error=pending_error=scan_error=0;current_generation=7;
    nexus::lifecycle::foreground=true;++cases;
    removed=staging=false;
}
void StartAndWait(){
    StartPcInstallDetection();
    Wait([]{return detection.done.load();});
}
int main(){
    Reset();removed=true;StartAndWait();auto deletion=TakeDetectedPcRemovals();
    CHECK(deletion&&deletion->size()==1&&!scans&&!TakeDetectedPcInstalls());
    ResumePcInstallDetection();StartAndWait();CHECK(!TakeDetectedPcRemovals()&&TakeDetectedPcInstalls()&&scans==1);
    Reset();removed=true;StartAndWait();++current_generation;CHECK(!TakeDetectedPcRemovals());
    Reset();replacing=true;StartAndWait();CHECK(!TakeDetectedPcRemovals()&&!TakeDetectedPcInstalls()&&!scans);replacing=false;
    Reset();staging=removed=true;StartAndWait();CHECK(!TakeDetectedPcRemovals()&&!TakeDetectedPcInstalls()&&!scans);
    Reset();StartAndWait();auto found=TakeDetectedPcInstalls();
    CHECK(found&&found->titles.size()==2&&found->titles[0].record.application_id==1&&found->titles[1].record.application_id==1);
    CHECK(!TakeDetectedPcInstalls());StartPcInstallDetection();CHECK(created==1&&joined==1&&closed==1);
    for(bool* no:{&available,&compatible,&marker}){
        Reset();*no=false;StartAndWait();CHECK(!TakeDetectedPcInstalls()&&scans==0);
    }
    for(bool* yes:{&partial,&pending,&backup}){
        Reset();*yes=true;StartAndWait();CHECK(!TakeDetectedPcInstalls()&&scans==0);
    }
    for(bool* no:{&new_work,&scan_complete}){
        Reset();*no=false;StartAndWait();CHECK(!TakeDetectedPcInstalls()&&scans==1);
    }
    Reset();blocked=true;StartAndWait();CHECK(!TakeDetectedPcInstalls());
    Reset();scan_error=Fault;StartAndWait();CHECK(!TakeDetectedPcInstalls());
    Reset();pending_error=Fault;StartAndWait();CHECK(!TakeDetectedPcInstalls()&&!scans);
    for(bool swapped:{false,true}){
        Reset();StartAndWait();if(swapped)++current_generation;else available=false;CHECK(!TakeDetectedPcInstalls());
    }
    Reset();block_scan=true;StartPcInstallDetection();Wait([]{return scan_entered.load()!=0;});
    CHECK(!TakeDetectedPcInstalls());StopUsbLibraryStartupCheck();
    CHECK(joined==1&&closed==1&&clients==0&&!TakeDetectedPcInstalls());
    Reset();StartAndWait();nexus::lifecycle::foreground=false;CHECK(!TakeDetectedPcInstalls());
    Reset();StopUsbLibraryStartupCheck();StartPcInstallDetection();CHECK(created==0);
    Reset();CancelUsbLibraryStartupCheck();StartAndWait();CHECK(TakeDetectedPcInstalls());
    Reset();applet=true;StartPcInstallDetection();CHECK(created==0&&!TakeDetectedPcInstalls());
    Reset();fail_create=true;StartPcInstallDetection();CHECK(created==1&&!started);
    Reset();fail_start=true;StartPcInstallDetection();CHECK(created==1&&started==1&&!detection.started&&closed==1);
    Reset();StartAndWait();fail_join=true;try{StopUsbLibraryStartupCheck();CHECK(false);}catch(Result rc){CHECK(rc==Fault&&detection.started);}
    fail_join=false;StopUsbLibraryStartupCheck();CHECK(!detection.started);
    std::cout<<"PC install detection: "<<cases<<" cases, "<<assertions<<" checks passed\n";
}
