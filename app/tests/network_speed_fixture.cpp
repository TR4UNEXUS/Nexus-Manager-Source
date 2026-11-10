// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/network_rate.hpp"
#include "nexus/progress_safety.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
using u64=std::uint64_t;using s64=std::int64_t;
std::atomic<u64> now{};
u64 armGetSystemTick(){return now.load();}u64 armTicksToNs(u64 tick){return tick;}
#define SCOPED_MUTEX(p) std::scoped_lock lock_(*(p))
unsigned signals{},unprotects{};void ueventSignal(int*){++signals;}void nexusWatchdogUnprotect(){++unprotects;}
struct Clock{void Update(){}};
struct ProgressBox {
    std::mutex m_mutex;Clock m_timestamp;nexus::safety::ProgressState m_safety;
    s64 m_size{},m_offset{},m_last_offset{},m_speed{};bool m_detailed{},m_overall{},m_finishing{},m_protected{};
    std::string m_transfer,m_progress_note,m_byte_detail;
    nexus::network::RateMeter m_network_rate;
    std::stop_source m_stop_source;std::vector<int*> m_cancel_events;int event{};
    int* GetCancelEvent(){return &event;}
    nexus::network::RateSample Sample(){SCOPED_MUTEX(&m_mutex);return m_network_rate.Sample(now);}
    // DECLARATIONS
};
// METHODS
bool Near(double a,double b){return std::abs(a-b)<=std::max(1.0,std::abs(b))*1e-12;}
int main(){
    constexpr u64 MiB=1024*1024,ms=1'000'000,sec=1'000'000'000;
    nexus::network::RateMeter meter;
    meter.Add(100);assert(!meter.Sample(sec).active);
    meter.Begin(0);meter.Add(2*MiB);assert(!meter.Sample(499*ms).sampled);
    auto sample=meter.Sample(500*ms);assert(sample.active&&sample.sampled&&sample.received==2*MiB&&Near(sample.bytes_per_second,4*MiB));
    // A UI stall uses real elapsed time, not an assumed one-second tick.
    meter.Add(3*MiB);sample=meter.Sample(2*sec);assert(Near(sample.bytes_per_second,2*MiB)&&sample.received==5*MiB);
    sample=meter.Sample(2500*ms);assert(sample.bytes_per_second==0&&sample.received==5*MiB);
    meter.Add(4*MiB);sample=meter.Sample(3*sec);assert(Near(sample.bytes_per_second,8*MiB));
    meter.End();assert(!meter.Sample(4*sec).active);meter.Add(MiB);assert(!meter.Sample(5*sec).active);
    meter.Begin(5*sec);sample=meter.Sample(5500*ms);assert(sample.received==0&&sample.bytes_per_second==0);
    // Defensive clock/overflow behavior cannot produce negative/fictitious rates.
    meter.Add(MiB);sample=meter.Sample(4*sec);assert(!sample.sampled&&sample.bytes_per_second==0);
    sample=meter.Sample(4500*ms);assert(sample.sampled&&sample.bytes_per_second==0);
    meter.Add(UINT64_MAX);meter.Add(100);sample=meter.Sample(5*sec);assert(sample.received==UINT64_MAX&&std::isfinite(sample.bytes_per_second));
    // Real progress setters must not count seeks, decompressed/local writes or
    // verifying already-installed content as newly downloaded NAS payload.
    now=0;ProgressBox box;box.BeginNetworkTransfer();box.AddNetworkBytes(2*MiB);
    box.NewTransfer("first.nca").UpdateTransfer(300*MiB,600*MiB);
    box.SetProgressNote("Keep the network and storage connected.");
    now=500*ms;sample=box.Sample();assert(sample.received==2*MiB&&Near(sample.bytes_per_second,4*MiB));
    box.NewTransfer("Verify reused USB content").UpdateTransfer(600*MiB,600*MiB);
    box.ResetTranfser();box.UpdateTransfer(7,10);now=sec;
    sample=box.Sample();assert(sample.received==2*MiB&&sample.bytes_per_second==0&&!box.m_progress_note.empty());
    box.AddNetworkBytes(MiB);box.NewTransfer("backward metadata seek").UpdateTransfer(1,5);
    now=1500*ms;sample=box.Sample();assert(sample.received==3*MiB&&Near(sample.bytes_per_second,2*MiB));
    box.SetDetailedProgress(50,"Verify","local work","still connected");
    box.SetOverallTransfer(1,2,"child",false);box.NewTransfer("child").UpdateTransfer(100,100);
    now=2*sec;sample=box.Sample();assert(sample.received==3*MiB&&sample.bytes_per_second==0&&box.m_offset==50);
    // Concurrent worker writes and render samples use the production mutex.
    now=3*sec;ProgressBox concurrent;concurrent.BeginNetworkTransfer();
    std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.emplace_back([&]{for(int i=0;i<10000;++i)concurrent.AddNetworkBytes(256);});
    for(int i=0;i<1000;++i)assert(concurrent.Sample().active);
    for(auto& thread:workers)thread.join();
    now=3500*ms;
    sample=concurrent.Sample();assert(sample.received==4*10000*256&&Near(sample.bytes_per_second,2.0*sample.received));
    // Cancellation hides stale rate but is not worker completion. Completion
    // also clears telemetry so a reused box cannot leak the previous speed.
    int event{};concurrent.m_cancel_events.push_back(&event);concurrent.RequestExit();
    assert(!concurrent.Sample().active&&concurrent.m_stop_source.stop_requested()&&!concurrent.m_safety.CanDismiss()&&signals==2);
    concurrent.m_protected=true;concurrent.WorkerFinished();assert(concurrent.m_safety.CanDismiss()&&unprotects==1&&!concurrent.Sample().active);
    box.EndNetworkTransfer();box.BeginNetworkTransfer();assert(box.Sample().received==0&&!box.Sample().sampled);
    ProgressBox local;local.NewTransfer("SD file").UpdateTransfer(512,1024);assert(!local.Sample().active&&local.m_offset==512);
    std::cout<<"NAS payload rate: wall-clock samples, idle/verification, seek resets, concurrency and cancellation/completion passed\n";
}
