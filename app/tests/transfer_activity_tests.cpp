// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer_activity.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace nexus::transfer::activity;
int main(){
    Journal journal;journal.Start("FTP",0);
    const auto id=journal.Open("/private/folder/Game.nsp","USB",-1,0);
    journal.Bytes(id,1048576,-1);
    auto s=journal.Read(500'000'000);
    assert(s.upload.name=="Game.nsp"&&s.upload.target=="USB"&&s.upload.active);
    assert(s.upload.expected==-1&&s.rate.bytes_per_second==2097152&&s.rate.received==1048576);
    assert(s.uploaded==0&&s.installed==0&&s.queued==0);
    assert(journal.Read(1'000'000'000).rate.bytes_per_second==0); // a pause is not a positive rate
    journal.Close(id,true,true,0,1'000'000'000);s=journal.Read(1'000'000'000);
    assert(s.uploaded==1&&s.queued==1&&s.installed==0&&!s.upload.active);
    journal.BeginInstall("/private/folder/Game.nsp","USB",1'000'000'000);
    journal.Stage("Quick USB check",2'000'000'000);const auto lines=journal.Read().lines.size();
    journal.Stage("Quick USB check",2'000'000'001);assert(journal.Read().lines.size()==lines);
    assert(journal.Read().installing&&journal.Read().queued==0);
    journal.FinishInstall(0,3'000'000'000);s=journal.Read(3'000'000'000);
    assert(s.installed==1&&!s.installing&&s.lines.back().text.starts_with("Installed successfully:"));
    journal.FinishInstall(0);assert(journal.Read().installed==1); // no duplicate success
    journal.Start("Cable",0);
    const auto partial=journal.Open("Broken.nsz","SD",100,0);
    journal.Bytes(partial,50,100);journal.Close(partial,false,false,0,1);
    s=journal.Read(2);assert(!s.uploaded&&!s.installed&&!s.queued&&s.errors==1);
    journal.BeginInstall("Error.xci","SD",3);journal.FinishInstall(0x1759,4);
    s=journal.Read(5);assert(s.errors==2&&!s.installed&&s.stage=="Installation stopped");
    assert(s.lines[s.lines.size()-2].text.find("0x00001759")!=std::string::npos);
    // Later connections cannot accidentally update a previous session's file.
    journal.Start("FTP again",0);journal.Bytes(partial,999,1000);assert(journal.Read(1).rate.received==0);
    for(unsigned n=0;n<150;++n)journal.Event(std::string(1000,'W'),n);
    s=journal.Read(200);assert(s.lines.size()==128&&s.lines[0].text.size()==223);
    Cursor cursor;cursor.Scroll(s,-1);const auto end=cursor.End(s),anchor=cursor.anchor;
    journal.Event("A later message",201);auto later=journal.Read(202);
    assert(cursor.anchor==anchor&&later.lines[cursor.End(later)-1].id==s.lines[end-1].id);
    for(unsigned n=0;n<200;++n)journal.Event("Evict old messages",n+203);
    later=journal.Read(500);assert(cursor.End(later)==Cursor::Rows);
    cursor.Follow();assert(cursor.End(later)==later.lines.size());
    assert(Name("/folder/control\nname.nsp")=="control name.nsp");
    const std::string unicode="\xe7\x95\x8c";
    assert(Clean("ab"+unicode+"test",4)=="ab..."); // no truncated UTF-8 glyph
    journal.Start("FTP",0);journal.BeginInstall("Game.nsz","USB",0);journal.Streaming();
    journal.ContentFile("00112233.ncz","Program","Writing NCA",1000,400,0);
    journal.ContentBytes(500,1000);auto content=journal.Read(500'000'000);
    assert(content.streaming&&content.content.size==1000&&content.content.packed==400&&content.content.bytes==500);
    assert(content.content.rate.bytes_per_second==1000&&content.rate.received==0); // expanded != uploaded bytes
    journal.ContentPhase("Flushing to USB",600'000'000);content=journal.Read(700'000'000);
    assert(!content.content.measuring&&!content.content.rate.active&&content.content.bytes==500);
    journal.ContentFile("00112233.ncz","Program","Quick USB check",1000,0,800'000'000);
    journal.ContentPhase("Quick check: header + size + write proof",800'000'001);
    content=journal.Read(900'000'000);assert(!content.content.measuring&&content.content.bytes==0); // no fictitious full read
    journal.FinishInstall(0x402);assert(!journal.Read().installed);
    assert(journal.Read().lines.back().text.find("Resend the original")!=std::string::npos);
    // Upload, install and drawing run on different threads in the application.
    journal.Start("Concurrent transfers",0);
    std::vector<std::thread> threads;
    for(int n=0;n<4;++n)threads.emplace_back([&,n]{
        const auto file=journal.Open(std::to_string(n)+".nsp",n%2?"SD":"USB",1000);
        for(int i=0;i<1000;++i)journal.Bytes(file,1,1000);
        journal.Close(file,true,true,0);
    });
    threads.emplace_back([&]{for(int i=0;i<1000;++i){auto snap=journal.Read();assert(snap.lines.size()<=128&&snap.active_uploads<=4);}});
    for(auto& thread:threads)thread.join();
    s=journal.Read();assert(s.rate.received==4000&&s.uploaded==4&&s.queued==4&&s.active_uploads==0);
    journal.Stop();s=journal.Read();assert(s.stopped&&s.queued==0);
    std::cout<<"Live activity: truthful completion, SD/USB labels, unknown/known sizes, stalled rates, failure codes, UTF-8, bounded history, scrolling and concurrent updates passed\n";
}
