// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer.hpp"
#include "nexus/manager.hpp"
#include "nexus/lifecycle.hpp"
#include "app.hpp"
#include "ui/option_box.hpp"
#include <atomic>
namespace nexus::transfer {
namespace {std::atomic<sphaira::ui::ProgressBox*> progress{};}
// UI-thread poll and destruction: never dereference a worker-owned/destroyed box.
void PollInstall(){if(!lifecycle::CanPoll())if(auto* p=progress.load())p->RequestExit();}
void ShowNextInstall(){
    InstallJob job;if(!TakeInstall(job))return;
    sphaira::App::Push<sphaira::ui::ProgressBox>(0,job.usb?"Installing to USB":"Installing to SD",job.path,[job](sphaira::ui::ProgressBox* p)->Result{
        progress=p;struct Clear{~Clear(){progress=nullptr;}} clear;
        R_TRY(p->ShouldExitResult());
        nexus::install::Diagnostic diagnostic{};
        const auto rc=sphaira::nexusapp::Install(p,job.usb,job.generation,job.path,job.usb?Target::Usb:Target::Sd,job.generation,&diagnostic);
        nexus::Log("pc-transfer-install",rc,job.size);return rc;
    },[](Result rc){
        FinishInstall(rc);
        if(R_FAILED(rc))sphaira::App::PushErrorBox(rc,"Installation paused. The uploaded package is kept in transfers/install. Check space, keys and the connection, then retry from Install games. An upload completing on your PC does not mean the game installed successfully.");
    });
}
}
