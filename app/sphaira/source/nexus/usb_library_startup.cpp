// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_library_startup.hpp"
#include "nexus/routing_backup.hpp"
#include "nexus/usb_import.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/nx_Client.hpp"
#include "utils/thread.hpp"
#include "app.hpp"
#include <atomic>
#include <utility>

namespace sphaira::nexusapp {
namespace {
struct StartupLibraryJob {
    Thread thread{};
    std::atomic_bool cancel{},done{};
    bool attempted{},started{},consumed{};
    u64 generation{};
    UsbLibraryStartupResult result;
} startup_library;

bool Stopped(){return startup_library.cancel.load(std::memory_order_relaxed)||!nexus::lifecycle::CanPoll();}

UsbLibraryStartupResult ReadStartupLibrary(){
    UsbLibraryStartupResult out;
    if(Stopped())return out;
    const auto pending_result=UsbImportPending(out.pending);
    if(Stopped())return {};
    // Recovery uses the current SD/system backup, even without the USB drive.
    if(R_SUCCEEDED(pending_result)&&out.pending)return out;
    nexus::Client usb;nexus::Status status{};
    if(R_FAILED(usb.Query(status))||!status.Available()||Stopped())return {};
    startup_library.generation=status.generation;
    if(R_FAILED(pending_result)){
        out.result=pending_result;
        out.detail="Could not check the previous USB repair state. Keep any recovery backups and open Repair USB games to review it.";
        return out;
    }
    if(R_SUCCEEDED(MissingRoutingWithConsoleBackup(out.console_backup_available))&&out.console_backup_available)return out;
    UsbImportScan scan;
    const auto rc=ScanUsbImports(nullptr,scan,status.generation,&startup_library.cancel,false);
    if(Stopped())return {};
    if(R_FAILED(rc)||!scan.complete){
        out.result=R_FAILED(rc)?rc:nexus::Invalid;
        out.detail=scan.detail.empty()?"The USB library check could not finish. Open Repair USB games to check again. No game data was changed.":scan.detail;
        return out;
    }
    const auto summary=SummarizeUsbLibrary(scan);
    out.affected_games=summary.affected_games;
    out.repairable_games=summary.repairable_games;
    out.blocked_games=summary.blocked_games;
    out.missing_ticket_games=summary.missing_ticket_games;
    return out;
}
void StartupLibraryWorker(void*){
    // ReadStartupLibrary releases all service handles and metadata buffers
    // before publishing completion. Only a small summary reaches the UI.
    auto result=ReadStartupLibrary();
    if(!Stopped())startup_library.result=std::move(result);
    startup_library.done.store(true,std::memory_order_release);
}
void JoinStartupLibrary(){
    if(!startup_library.started)return;
    const auto waited=threadWaitForExit(&startup_library.thread);
    if(R_FAILED(waited))fatalThrow(waited); // Never continue while a worker owns services.
    const auto closed=threadClose(&startup_library.thread);
    if(R_FAILED(closed))fatalThrow(closed);
    startup_library.started=false;
}
}

void StartUsbLibraryStartupCheck(){
    if(startup_library.attempted)return;
    startup_library.attempted=true;
    if(App::IsApplet()||!nexus::lifecycle::CanPoll()){
        startup_library.done.store(true,std::memory_order_release);return;
    }
    // An initial applet focus callback can run before the first menu frame.
    // That event cancels no live worker; it must not suppress the later first
    // foreground check. Stop() still suppresses it by setting attempted.
    startup_library.cancel.store(false,std::memory_order_relaxed);
    const auto created=utils::CreateThread(&startup_library.thread,StartupLibraryWorker,nullptr,256*1024);
    if(R_FAILED(created)){
        startup_library.done.store(true,std::memory_order_release);return;
    }
    const auto started=threadStart(&startup_library.thread);
    if(R_FAILED(started)){
        threadClose(&startup_library.thread);
        startup_library.done.store(true,std::memory_order_release);return;
    }
    startup_library.started=true;
}
bool TakeUsbLibraryStartupCheck(UsbLibraryStartupResult& out){
    if(startup_library.consumed||!startup_library.done.load(std::memory_order_acquire))return false;
    JoinStartupLibrary();startup_library.consumed=true;
    if(Stopped()){startup_library.result={};return false;}
    // A result may have waited behind a browser/dialog. Do not report the
    // previous drive's games after unplugging or replacing it. Recovery of an
    // existing SD journal is deliberately independent of USB availability.
    if(!startup_library.result.pending&&(startup_library.result.console_backup_available||startup_library.result.affected_games||R_FAILED(startup_library.result.result))){
        nexus::Client usb;nexus::Status status{};
        if(R_FAILED(usb.Query(status))||!status.Available()||status.generation!=startup_library.generation){
            startup_library.result={};out={};return true;
        }
    }
    out=std::move(startup_library.result);
    return true;
}
void CancelUsbLibraryStartupCheck(){startup_library.cancel.store(true,std::memory_order_relaxed);}
void StopUsbLibraryStartupCheck(){
    startup_library.attempted=true;
    CancelUsbLibraryStartupCheck();JoinStartupLibrary();
    startup_library.result={};startup_library.consumed=true;
}
}
