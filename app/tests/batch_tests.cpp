// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/batch.hpp"
#include <cassert>
#include <iostream>
using namespace nexus;
using namespace nexus::batch;
static const std::vector<Entry> files{
    {"Base.nsp","/Games/Base.nsp",100},
    {"Update.NSZ","/Updates/Update.NSZ",200},
    {"DLC.nsp","/Games/DLC.nsp",30}};
static auto no_notify=[](std::size_t,bool){};
int main(){
    for(const auto* s:{"/Game.nsp","/Game.NSZ","/Game.XcI","/Game.xcz"})assert(IsPackage(s));
    for(const auto* s:{"/Game.nro","/Game.nsp.bak","/nsp","/Game.",""})assert(!IsPackage(s));
    Selection selected;
    assert(selected.Add(files[0]) && selected.Add(files[1]) && selected.Add(files[2]));
    assert(selected.Add(files[0]) && selected.Entries().size()==3 && selected.Bytes()==330);
    assert(selected.Index(files[1].path)==1 && selected.Index("/Missing.nsp")==3);
    assert(!selected.Move(0,false) && !selected.Move(2,true) && !selected.Move(3,false));
    assert(selected.Move(2,false) && selected.Entries()[1].path==files[2].path);
    assert(selected.Move(1,true) && selected.Entries()[2].path==files[2].path);
    selected.Remove(files[1].path);assert(selected.Entries().size()==2 && selected.Bytes()==130);
    for(const auto& e:std::vector<Entry>{{"Bad.nsp","/../Bad.nsp",1},{"Bad.nsp","/Bad.nsp",0},
        {"Other.nsp","/Bad.nsp",1},{"Bad.nsp","/Bad.nsp",UINT64_MAX},{"Bad.nro","/Bad.nro",1}})
        assert(!selected.Add(e));
    const Entry extra{"Extra.nsp","/Extra.nsp",20};
    const std::vector<Entry> invalid_all{extra,{"Zero.nsp","/Zero.nsp",0}};
    assert(!selected.AddAll(invalid_all));
    assert(!selected.Contains(extra.path) && selected.Entries().size()==2); // atomic select-all
    selected.Clear();assert(selected.Empty());
    for(std::size_t i=0;i<MaxPackages;++i){const auto n=std::to_string(i)+".nsp";assert(selected.Add({n,"/"+n,1}));}
    assert(!selected.Add(extra) && selected.Entries().size()==MaxPackages);
    assert(selected.Add(selected.Entries()[0])); // duplicate at capacity is not a new entry
    selected.Clear();
    assert(selected.Add({"Huge.nsp","/Huge.nsp",INT64_MAX}));
    assert(selected.Add({"Huge2.nsp","/Huge2.nsp",INT64_MAX}));
    assert(!selected.Add({"Overflow.nsp","/Overflow.nsp",2}));
    assert(selected.Bytes()==UINT64_MAX-1);

    // All sources feed the same engine; these tests validate the destination and
    // sequence contract, not Horizon IPC or package parsing.
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        Report report(files,target);std::vector<std::string> installed;int checked=0,started=0,finished=0;
        const auto rc=Run(report,[]{return 0u;},[&](const Entry&){++checked;return 0u;},
            [&](const Entry& e){assert(report.target==target);installed.push_back(e.path);return 0u;},
            [&](std::size_t i,bool starting){assert(i<files.size());if(starting)++started;else ++finished;});
        assert(rc==0 && report.result==0 && report.stage=="Complete");
        assert(report.Count(State::Installed)==3 && report.Count(State::Pending)==0);
        assert(checked==6 && started==3 && finished==3);
        for(std::size_t i=0;i<files.size();++i)assert(installed[i]==files[i].path);
    }
    {
        Report r(files,Target::Usb);unsigned installs=0;
        assert(Run(r,[]{return 0u;},[](const Entry& e){return e.path==files[2].path?7u:0u;},
            [&](const Entry&){++installs;return 0u;},no_notify)==7);
        assert(installs==0 && r.items[2].state==State::Failed && r.Count(State::Pending)==2);
        assert(r.stage=="Check source packages" && r.result==7 && r.items[2].result==7);
    }
    {
        Report r(files,Target::Sd);unsigned installs=0;
        assert(Run(r,[]{return 0u;},[](const Entry&){return 0u;},
            [&](const Entry&){return ++installs==2?8u:0u;},no_notify)==8);
        assert(installs==2 && r.items[0].state==State::Installed && r.items[1].state==State::Failed);
        assert(r.items[2].state==State::Pending && r.result==8);
    }
    {
        Report r(files,Target::Internal);unsigned installs=0;
        assert(Run(r,[]{return 9u;},[](const Entry&){return 0u;},
            [&](const Entry&){++installs;return 0u;},no_notify)==9);
        assert(installs==0 && r.items[0].state==State::Cancelled && r.Count(State::Pending)==2);
    }
    {
        Report r(files,Target::Internal);unsigned installs=0;bool cancel=false;
        assert(Run(r,[&]{return cancel?9u:0u;},[](const Entry&){return 0u;},
            [&](const Entry&){++installs;cancel=true;return 0u;},no_notify)==9);
        assert(installs==1 && r.items[0].state==State::Installed && r.items[1].state==State::Cancelled);
        assert(r.items[2].state==State::Pending);
    }
    {
        Report r(files,Target::Usb);unsigned installs=0;bool cancel=false;
        assert(Run(r,[&]{return cancel?9u:0u;},[](const Entry&){return 0u;},
            [&](const Entry&){++installs;cancel=true;return 9u;},no_notify)==9);
        assert(installs==1 && r.items[0].state==State::Cancelled && r.Count(State::Pending)==2);
    }
    {
        Report r(files,Target::Usb);unsigned installs=0,generation=17;
        assert(Run(r,[]{return 0u;},[&](const Entry&){return generation==17?0u:10u;},
            [&](const Entry&){++installs;++generation;return 0u;},no_notify)==10);
        assert(installs==1 && r.items[0].state==State::Installed && r.items[1].state==State::Failed);
        assert(r.items[2].state==State::Pending); // reconnect never redirects remaining work
    }
    {
        Report failed(files,Target::Usb);
        failed.items[1].detail="Expected/actual size mismatch for the failed NCA";
        assert(failed.items[0].detail.empty() && failed.items[2].detail.empty());
        // A fresh reinstall uses a fresh report; no filename/title blacklist.
        Report retry(files,Target::Usb);
        assert(Run(retry,[]{return 0u;},[](const Entry&){return 0u;},
            [](const Entry&){return 0u;},no_notify)==0);
        for(const auto& row:retry.items)assert(row.detail.empty() && row.state==State::Installed);
    }
    std::cout<<"Batch tests passed: selection/order/limits, 3 targets, preflight, partial failure, cancellation, media change, per-attempt details\n";
}
