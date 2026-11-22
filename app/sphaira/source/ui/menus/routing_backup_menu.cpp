#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/routing_backup.hpp"
#include "nexus/index_recovery.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
namespace sphaira::nexusapp {
namespace {
void Message(const std::string&text,Result rc=0){
    std::string message=text;if(R_FAILED(rc)){char code[48]{};snprintf(code,sizeof(code),"\n\nDiagnostic code: 0x%08X"_nx,rc);message+=code;}
    App::Push<ui::OptionBox>(message,"Close"_nx,[](auto){});
}
void Restore(){
    auto plan=std::make_shared<RecoveryPlan>();
    App::Push<ui::ProgressBox>(0,"Check console backup"_nx,"Checking before changing anything"_nx,[plan](ui::ProgressBox*p){return ScanConsoleRoutingRestore(p,*plan);},[plan](Result rc){
        if(R_FAILED(rc)){Message(plan->detail,rc);return;}
        App::Push<ui::OptionBox>(plan->detail,"Cancel"_nx,"Restore links"_nx,0,[plan](auto choice){
            if(!choice||*choice!=1)return;
            App::Push<ui::ProgressBox>(0,"Restore USB game links"_nx,"Game files and saved progress are kept"_nx,[plan](ui::ProgressBox*p){return ApplyConsoleRoutingRestore(p,*plan);},[plan](Result result){Message(plan->detail,result);});
        });
    },ui::ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
}
void DisplayConsoleRoutingBackup(){
    auto menu=std::make_unique<ui::Sidebar>("USB library backup"_nx,"Stored on this system, separate from game saves"_nx,ui::Sidebar::Side::RIGHT,1280.f);
    menu->Add<ui::SidebarEntryCallback>("Check backup status"_nx,[]{
        auto status=std::make_shared<nexus::routing_backup::Status>();
        App::Push<ui::ProgressBox>(0,"Check library backup"_nx,"Reading the console save"_nx,[status](ui::ProgressBox*){return QueryConsoleRoutingBackup(*status);},[status](Result rc){
            if(R_FAILED(rc)){Message("Could not read the console backup. This feature needs the matching Nexus N4 firmware. Existing game files and saves were kept."_nx,rc);return;}
            std::string text=status->flags&1?"A checked USB library backup is saved on this system.\n\nIt can restore missing routing files if the atmosphere folder is deleted. Reinstall the Nexus firmware first, then choose Restore missing USB links."_nx:"No console backup is available yet. The firmware saves valid existing mappings automatically after startup. You can also choose Save current USB links."_nx;
            if(status->last_result)text+="\n\nThe most recent save attempt could not finish. The previous backup was kept. Use Repair USB games to check the current library, then save again."_nx;
            Message(text);
        },ui::ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    },"See whether this sysMMC or emuMMC has a valid recovery copy."_nx);
    menu->Add<ui::SidebarEntryCallback>("Save current USB links"_nx,[]{
        App::Push<ui::ProgressBox>(0,"Save USB library backup"_nx,"Checking current mappings"_nx,[](ui::ProgressBox*){u64 sequence{};return ConfirmConsoleRoutingBackup(sequence);},[](Result rc){Message(R_SUCCEEDED(rc)?"USB library backup was saved and read back from this system's dedicated Nexus save. It matches the current SD routing files."_nx:"The current mappings could not be backed up and verified. Any previous valid backup was kept. Run Repair USB games, then try again."_nx,rc);});
    },"Refresh the automatic backup now. Game files, keys, tickets and saves are not copied."_nx);
    menu->Add<ui::SidebarEntryCallback>("Restore missing USB links"_nx,[]{Restore();},"Check the saved links against this system and the connected drive before restoring."_nx);
    menu->Add<ui::SidebarEntryCallback>("What is backed up?"_nx,[]{Message("The backup contains the three USB routing indexes.\n\nIt is stored in a dedicated Nexus system save and keeps two checked revisions. Game files, keys, tickets and saved progress are not included. Each sysMMC or emuMMC has its own backup.\n\nDeleting the atmosphere folder does not delete this save. Deleting or recreating the whole emuMMC can still remove it.\n\nRestore only fills a missing set of routing files. Existing mappings are kept; use Repair USB games to fix those."_nx);});
    App::Push(std::move(menu));
}
}
