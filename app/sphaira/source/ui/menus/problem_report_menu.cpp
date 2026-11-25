// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/problem_reports.hpp"
#include "nexus/localize.hpp"
#include "nexus/diagnostics.hpp"
#include "nexus/nx_Client.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "swkbd.hpp"
namespace nexus::reports {
namespace {
using namespace sphaira;using namespace sphaira::ui;
void Message(const std::string& text,Result rc=0){std::string value=text;if(R_FAILED(rc)){char code[40]{};snprintf(code,sizeof(code),"\n\n0x%08X",rc);value+=code;}App::Push<OptionBox>(value,"Close"_nx,[](auto){});}
}
void DisplayReportMenu(){
    auto menu=std::make_unique<Sidebar>("Report a problem"_nx,"Choose what to share. Reports help improve Manager and Nexus USB firmware."_nx,Sidebar::Side::RIGHT,1280.f);
    menu->Add<SidebarEntryCallback>("Reporting status"_nx,[]{Message(Status());},"View automatic reporting, queued reports and the last upload receipt."_nx);
    menu->Add<SidebarEntryCallback>("Automatic reporting"_nx,[]{
        App::Push<OptionBox>("Automatically report failed tasks, recent crashes and returning to Manager?\n\nIncludes versions, game IDs, error codes, transfer stages, USB status, battery samples, connection type and configured module IDs. Keys, tickets, passwords, addresses, game files, saves and raw memory are excluded. Reports are kept for up to 30 days.\n\nUploads wait until Manager is idle. You can turn this off here."_nx,"Turn off"_nx,"Enable"_nx,0,[](auto choice){if(!choice)return;const auto rc=SetPreference(*choice==1);Message(R_SUCCEEDED(rc)?"Reporting preference saved."_nx:"Could not save the reporting preference. Automatic uploads are stopped for this session. Check the SD card and try again."_nx,rc);});
    },"Choose whether Manager can share diagnostic reports while idle."_nx);
    menu->Add<SidebarEntryCallback>("Send a problem report"_nx,[]{
        if(!Configured()){Message("Uploads are not configured in this build. Use Export diagnostics to save a ZIP."_nx);return;}
        std::string description;if(R_FAILED(swkbd::ShowText(description,"What went wrong?"_nx,"Include what you were doing and the error. Leave out passwords and private details."_nx,nullptr,1,500)))return;
        App::Push<OptionBox>(description+"\n\n"+"Send this description with diagnostic events, versions, game IDs, USB status, battery samples, connection type and configured module IDs? Game files, keys, tickets, passwords, addresses, saves and raw memory are excluded."_nx,"Cancel"_nx,"Send report"_nx,0,[description](auto choice){
            if(!choice||*choice!=1)return;auto preview=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Preparing report"_nx,"Collecting diagnostic information"_nx,[description,preview](ProgressBox*){return QueueManual(description,*preview);},[preview](Result rc){Message(R_SUCCEEDED(rc)?*preview:"Could not queue the report. Nothing was uploaded. Check the SD card or export diagnostics instead."_nx,rc);});
        });
    },"Describe a problem and queue one report even when automatic reporting is off."_nx);
    menu->Add<SidebarEntryCallback>("Discard queued reports"_nx,[]{App::Push<OptionBox>("Delete the reports that have not been uploaded yet? Existing logs and uploaded reports are kept."_nx,"Cancel"_nx,"Discard"_nx,0,[](auto choice){if(!choice||*choice!=1)return;const auto rc=ClearQueued();Message(R_SUCCEEDED(rc)?"Queued reports discarded."_nx:"Could not clear the queue. Check the SD card and try again."_nx,rc);});});
    menu->Add<SidebarEntryCallback>("Export diagnostics"_nx,[]{auto path=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Export diagnostics"_nx,"Saving a ZIP on the SD card"_nx,[path](ProgressBox*p){return diagnostics::Export(p,*path);},[path](Result rc){Message(R_SUCCEEDED(rc)?*path:"Diagnostic export could not finish."_nx,rc);});},"Keep the existing diagnostic ZIP for manual review or sharing."_nx);
    App::Push(std::move(menu));
}
}
