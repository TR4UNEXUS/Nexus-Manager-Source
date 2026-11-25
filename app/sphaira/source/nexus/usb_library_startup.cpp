// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_library_startup.hpp"
#include "nexus/usb_import.hpp"
#include "nexus/game_cleanup.hpp"
#include "nexus/hats.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/routing_backup.hpp"
#include "utils/thread.hpp"
#include "app.hpp"
#include <algorithm>
#include <atomic>
#include <set>

namespace sphaira::nexusapp {
namespace {
struct Detection {
    Thread thread{};
    std::atomic_bool cancel{}, done{};
    bool attempted{}, started{};
    bool checked_removals{};
    std::shared_ptr<std::vector<PcRemoval>> removals;
    std::shared_ptr<UsbImportScan> ready;
} detection;

bool Stopped() { return detection.cancel.load() || !nexus::lifecycle::CanPoll(); }

void SelectNewFamilies(UsbImportScan& scan) {
    const auto plan = PlanUsbLibrary(scan);
    std::set<u64> additions;
    for (size_t i=0; i<scan.titles.size(); ++i) {
        const auto& row = scan.titles[i];
        if (plan.selected[i] && row.needs_home_import) additions.insert(row.record.application_id);
    }
    // Keep every sibling (including native versions and blockers) for the normal
    // transaction planner. Never automatically repair an unrelated ready item.
    std::erase_if(scan.titles, [&](const auto& row) {
        const auto family = row.record.application_id;
        return !additions.contains(family);
    });
}

void DetectPcInstalls(void*) {
    auto detect = []() -> std::shared_ptr<UsbImportScan> {
        if (Stopped() || !nexus::hats::SupportsPcLibrary()) return {};
        nexus::Client usb;
        nexus::Status status{};
        // A bus-powered drive can still be mounting when Manager opens.
        for (int attempt=0; attempt<10; ++attempt) {
            if (Stopped() || R_FAILED(usb.Query(status))) return {};
            if (status.Available()) break;
            svcSleepThread(200'000'000);
        }
        if (!status.Available() || Stopped()) return {};
        // PC Manager 0.1.x keeps this empty session file even for packages with
        // multiple titles, no ticket or no display-name hint. A newly discovered
        // drive is scanned read-only; only the foreground import writes records.
        nexus::Info session{};
        if (R_FAILED(usb.Stat(status.generation,"/nexus/pc-import/v1/session.lock",session))
            || session.directory || session.size || Stopped()) return {};
        nexus::Info partial{};
        if (usb.Stat(status.generation,"/nexus/pc-import/v1/content.partial",partial) != MAKERESULT(140,205)) return {};
        for(const auto* name:{"/nexus/pc-import/v1/install-stage","/nexus/pc-import/v1/install-ready","/nexus/pc-import/v1/replace-stage","/nexus/pc-import/v1/replace-ready"})
            if(usb.Stat(status.generation,name,partial)!=MAKERESULT(140,205))return {};
        bool pending{}, needs_backup{};
        if (R_FAILED(UsbImportPending(pending)) || pending || Stopped()) return {};
        if (R_FAILED(MissingRoutingWithConsoleBackup(needs_backup)) || needs_backup || Stopped()) return {};
        if(!detection.checked_removals){
            auto removed=std::make_shared<std::vector<PcRemoval>>();
            const auto rc=ListPcRemovals(nullptr,*removed,&detection.cancel);
            if(R_FAILED(rc)){nexus::Log("pc-startup-removals",rc);return {};}
            if(!removed->empty()){detection.removals=std::move(removed);return {};}
        }
        auto scan = std::make_shared<UsbImportScan>();
        if (R_FAILED(ScanUsbImports(nullptr,*scan,status.generation,&detection.cancel,false)) || !scan->complete || Stopped()) return {};
        SelectNewFamilies(*scan);
        return scan->titles.empty() ? nullptr : scan;
    };
    detection.ready = detect();
    detection.done.store(true,std::memory_order_release);
}

void JoinDetection() {
    if (!detection.started) return;
    const auto waited = threadWaitForExit(&detection.thread);
    if (R_FAILED(waited)) fatalThrow(waited);
    const auto closed = threadClose(&detection.thread);
    if (R_FAILED(closed)) fatalThrow(closed);
    detection.started = false;
}
}

void StartPcInstallDetection() {
    if (detection.attempted || !nexus::lifecycle::CanPoll()) return;
    detection.attempted = true;
    if (App::IsApplet()) return;
    detection.cancel.store(false);
    if (R_FAILED(utils::CreateThread(&detection.thread,DetectPcInstalls,nullptr,256*1024))) return;
    if (R_FAILED(threadStart(&detection.thread))) { threadClose(&detection.thread); return; }
    detection.started = true;
}
std::shared_ptr<UsbImportScan> TakeDetectedPcInstalls() {
    if (!detection.started || !detection.done.load(std::memory_order_acquire)) return {};
    JoinDetection();
    auto scan = std::move(detection.ready);
    if (!scan || Stopped()) return {};
    nexus::Client usb;
    if (R_FAILED(usb.Check(scan->generation))) return {};
    return scan;
}
std::shared_ptr<std::vector<PcRemoval>> TakeDetectedPcRemovals(){
    if(!detection.started||!detection.done.load(std::memory_order_acquire)||!detection.removals)return {};
    JoinDetection();
    auto receipts=std::move(detection.removals);
    nexus::Client usb;
    if(Stopped()||R_FAILED(usb.Check(receipts->front().generation)))return {};
    return receipts;
}
void ResumePcInstallDetection(){
    if(!detection.started){detection.checked_removals=true;detection.attempted=false;}
}
void CancelUsbLibraryStartupCheck() { detection.cancel.store(true); }
void StopUsbLibraryStartupCheck() {
    detection.attempted = true;
    CancelUsbLibraryStartupCheck();
    JoinDetection();
    detection.ready.reset();
    detection.removals.reset();
}
}
