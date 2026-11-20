// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/progress_presentation.hpp"
#include "nexus/network_rate.hpp"
#include "nexus/task_progress.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace nexus::progressui;
    nexus::network::RateMeter rate;ByteCursor cursor;nexus::TaskProgress work;
    constexpr unsigned long long MiB=1024*1024;
    rate.Begin(0);
    for(unsigned file=0;file<50;++file){
        cursor.Reset();work.Push(file*200,(file+1)*200);
        for(unsigned part=1;part<=4;++part){
            rate.Add(cursor.Advance(part*MiB,4*MiB));
            assert(cursor.Advance(part*MiB,4*MiB)==0); // duplicate callbacks
            work.Set(part,4); // weights never enter the byte meter
        }
        work.Pop();
    }
    auto s=rate.Sample(10'000'000'000ULL);
    assert(s.received==200*MiB&&s.bytes_per_second==20*MiB);
    assert(work.value==9999); // publication still pending, not 50 completed bars
    s=rate.Sample(11'000'000'000ULL);assert(s.bytes_per_second==0);
    assert(cursor.Advance(5*MiB,4*MiB)==0); // invalid completion cannot inflate speed
    cursor.Reset();assert(cursor.Advance(MiB,4*MiB)==MiB);
    work.Finish(false);assert(work.value==9999);
    work.Finish(true);assert(work.value==10000);
    assert(Status("Verify reused USB content","Install to USB")=="Checking files");
    assert(Status("Pushing application record","Install to SD")=="Adding game to HOME");
    assert(Status("Flushing to USB","Install")=="Saving files");
    assert(Status("0123456789.nca","Install 2 / 5")=="Installing game files");
    assert(Status("Copying /long/path","Move SD to USB")=="Copying files");
    assert(Status("Deleting /long/path","Remove port")=="Removing files");
    std::cout<<"Transfer speed: 50 files, duplicate callbacks, pauses, invalid counts, weighted progress and simple stages passed\n";
}
