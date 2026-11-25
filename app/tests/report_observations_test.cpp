// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/report_observations.hpp"
#include <cassert>
#include <cstdio>
int main(){
    using namespace nexus::reports;
    const std::string name="01795147423_010013c00e930000.log";
    const std::string body="Atmosphere Crash Report (v1.7):\nResult: 0x299C02 (2002-5326)\nProcess Info:\n Program ID: 010013c00e930000\nException Info:\n Type: User Break\n Registers: PRIVATE_REGISTER_DUMP\n password: PRIVATE_PASSWORD\n";
    auto crash=Crash(name,body);assert(crash.valid&&crash.program==0x010013c00e930000&&crash.result==0x299c02&&crash.type==1);
    const auto fatal=Crash(name,"Atmosphere Fatal Report (v1.1):\nResult: 0x299C02\nProgram ID: 010013c00e930000\n");assert(fatal.valid&&fatal.type==4);
    assert(!Crash("../../private",body).valid);assert(!Crash(name,body+"Program ID: 0100000000000001\n").valid);
    auto changed=body;changed.replace(changed.find("010013c00e930000"),16,"010013c00e930001");assert(!Crash(name,changed).valid);
    assert(!Crash(name,"Atmosphere Crash Report\nProgram ID: 010013c00e930000\n").valid);
    const auto event=[](const char* phase,unsigned value){return std::string("NM2 2026-11-25T12:00:00Z version=0.2.32 session=0000000000000001 uptime_ms=100 operation=1 task=install event=")+phase+" rc=0x00000000 value="+std::to_string(value)+"\n";};
    std::string log=event("install-content-program-id",42)+event("install-package-title-version",65536);
    for(unsigned i=0;i<150;++i)log+=event("copy-progress",i);
    log+=event("report-origin",1);auto filtered=ManagerLines(log);
    assert(filtered.find("install-content-program-id")!=filtered.npos&&filtered.find("install-package-title-version")!=filtered.npos);
    assert(filtered.find("value=149")!=filtered.npos&&std::count(filtered.begin(),filtered.end(),'\n')<=80);
    assert(ManagerLines(filtered)==filtered&&IsAutomaticCapture(filtered));
    assert(!IsAutomaticCapture(filtered+event("report-origin",0)));
    assert(ManagerLines(body).empty());
    assert(ManagerLines(log+"password=PRIVATE_PASSWORD\n").find("PRIVATE_PASSWORD")==std::string::npos);
    puts("PASS bounded crash extraction, matching title IDs, context retention, origin consent and private-field rejection");
}
