// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/repair_assistant.hpp"
#include "nexus/repair_catalog.hpp"
#include "nexus/recovery_ui.hpp"
#include "nexus/routing_backup.hpp"
#include "nexus/usb_import.hpp"
#include "nexus/problem_reports.hpp"
#include "nexus/update.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include <map>
namespace sphaira::nexusapp {
namespace {
using namespace ui;
using Plan=std::shared_ptr<RecoveryPlan>;
using Guide=std::shared_ptr<nexus::catalog::Loaded>;
void Message(std::string value,Result rc=0){if(rc){char code[40]{};snprintf(code,sizeof(code),"\n\n0x%08X",rc);value+=code;}App::Push<OptionBox>(value,"Back"_nx,[](auto){});}
std::string Id(u64 id){char text[24]{};snprintf(text,sizeof(text),"%016llX",(unsigned long long)id);return text;}
void Advice(const Guide& guide,const nexus::catalog::Evidence& evidence){
    auto menu=std::make_unique<Sidebar>("Repair guidance"_nx,"Guidance is matched to local findings; existing verification and rollback checks still apply."_nx,Sidebar::Side::RIGHT,1280.f);
    const auto matches=nexus::catalog::Match(guide->catalog,evidence,nexus::updates::VersionCode(nexus::updates::Version));
    for(auto index:matches){const auto rule=guide->catalog.rules[index];menu->Add<SidebarEntryCallback>(i18n::get(rule.title),[rule]{Message(i18n::get(rule.advice));},rule.id);}
    if(matches.empty())menu->Add<SidebarEntryCallback>("No specific guidance found"_nx,[]{Message("The built-in file checks and repair tools remain available. Export or send a diagnostic report if a problem remains."_nx);});
    App::Push(std::move(menu));
}
void ResultMenu(const Plan& plan,const Guide& guide,Result rc){
    auto menu=std::make_unique<Sidebar>("USB repair result"_nx,"Review what was checked, repaired or left unchanged."_nx,Sidebar::Side::RIGHT,1280.f);
    menu->Add<SidebarEntryCallback>("Summary"_nx,[plan,rc]{Message(RecoverySummary(*plan,rc),rc);},plan->backup_detail);
    menu->Add<SidebarEntryCallback>("Full report"_nx,[plan]{Message(plan->report_text.empty()?plan->detail:plan->report_text);},plan->report_path);
    for(size_t i=0;i<std::min<size_t>(plan->issues.size(),50);++i){const auto issue=plan->issues[i];menu->Add<SidebarEntryCallback>(issue.name+" / "+issue.kind,[plan,i]{Message(RecoveryIssuePage(*plan,i));},"Review the file and reason; nothing is deleted by this report."_nx);}
    for(const auto& meta:plan->metadata)for(auto index:meta.files){const auto& file=plan->files[index];if(!file.verified_modified_meta)continue;
        nexus::catalog::Evidence evidence{nexus::catalog::Condition::ConvertedMetadata,meta.application_id,meta.key.version,u8(meta.key.type)};
        memcpy(evidence.content.data(),&file.info.content_id,16);evidence.sha256=file.actual_hash;
        menu->Add<SidebarEntryCallback>(meta.name+" - "+"Metadata guidance"_nx,[guide,evidence]{Advice(guide,evidence);},"This metadata passed the local integrity and content-reference checks."_nx);
    }
    menu->Add<SidebarEntryCallback>("USB library backup"_nx,[]{DisplayConsoleRoutingBackup();},"Check the saved map independently of the repair result."_nx);
    menu->Add<SidebarEntryCallback>("Report a problem"_nx,[]{nexus::reports::DisplayReportMenu();});
    App::Push(std::move(menu));
}
void Repair(const Plan& plan,const Guide& guide){
    if(!plan->ready){Message("This preview is no longer available. Scan again before repairing."_nx);return;}
    const auto prompt=plan->pending?plan->detail:
        "Repair the missing links shown in this preview?\n\nManager will verify the affected base game, updates and DLC before changing their links, then save and check the console backup. Files that cannot be verified are left unchanged.\n\nGame files and saves are kept. Close other games and installers and keep storage connected."_nx;
    auto answered=std::make_shared<bool>(false);
    App::Push<OptionBox>(prompt,"Cancel"_nx,plan->pending?"Restore backup"_nx:"Verify and repair"_nx,0,[plan,guide,answered](auto answer){
        if(*answered)return;*answered=true;
        if(!answer||*answer!=1)return;
        App::Push<ProgressBox>(0,"Repair USB games"_nx,"Verifying before changing links"_nx,[plan](ProgressBox*p){return ApplyUsbRecovery(p,*plan);},[plan,guide](Result rc){ResultMenu(plan,guide,rc);});
    });
}
void Preview(const Plan& plan,const Guide& guide,Result rc){
    std::map<u64,std::vector<RecoveryScanRecord>> games;std::set<u64> repairable;
    for(const auto& row:plan->scan_records)games[row.application].push_back(row);
    for(const auto& row:plan->metadata)repairable.insert(row.application_id?row.application_id:row.key.id);
    const auto summary=std::to_string(games.size())+" games reviewed / "_nx+std::to_string(repairable.size())+" queued for verification. No changes yet."_nx;
    auto menu=std::make_unique<Sidebar>("USB repair preview"_nx,summary,Sidebar::Side::RIGHT,1280.f);
    if(R_SUCCEEDED(rc)&&plan->ready)menu->Add<SidebarEntryCallback>(plan->pending?"Restore interrupted repair"_nx:"Verify and repair missing links"_nx,[plan,guide]{Repair(plan,guide);},"Review first; incomplete or changed files will stop their repair."_nx);
    menu->Add<SidebarEntryCallback>("Scan summary"_nx,[plan,rc]{Message(plan->detail,rc);},rc?"The scan could not finish. No changes were made."_nx:"Existing links are size-checked, not proof that every game can launch."_nx);
    for(const auto& [application,rows]:games){
        auto name=rows.front().name;if(name.empty())name=Id(application);
        std::string detail=name+" ["+Id(application)+"]\n\n";
        for(const auto& row:rows)detail+=Id(row.record)+" / v"+std::to_string(row.version)+"\n"+row.status+"\n\n";
        const bool candidate=repairable.contains(application);
        menu->Add<SidebarEntryCallback>(name,[application,detail]{App::Push<OptionBox>(detail,"Back"_nx,"Check this game"_nx,0,[application](auto answer){if(answer&&*answer==1)DisplayGuidedUsbRepair(application);});},candidate?"Ready for file verification before repair"_nx:rows.front().status);
    }
    menu->Add<SidebarEntryCallback>("Games missing from HOME"_nx,[]{DisplayUsbImport();},"Scan the drive and preview complete games that need registering in this system."_nx);
    menu->Add<SidebarEntryCallback>("USB library backup"_nx,[]{DisplayConsoleRoutingBackup();},"Check or restore the map saved separately from the atmosphere folder."_nx);
    nexus::catalog::Evidence evidence{};
    if(!repairable.empty())evidence.condition=nexus::catalog::Condition::MissingLinks;
    else if(plan->scan_records.empty())evidence.condition=nexus::catalog::Condition::MissingRegistration;
    if(!plan->before[0].exists&&!plan->before[1].exists&&!plan->before[2].exists)evidence.condition=nexus::catalog::Condition::MissingMaps;
    menu->Add<SidebarEntryCallback>("Repair guidance"_nx,[guide,evidence]{Advice(guide,evidence);},"Catalogue revision "_nx+std::to_string(guide->catalog.revision)+(guide->cached?" - saved copy"_nx:" - bundled copy"_nx));
    menu->Add<SidebarEntryCallback>("Update repair guidance"_nx,[]{
        nexus::reports::Stop();nexus::updates::StopStartupCheck();auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Updating repair guidance"_nx,"Checking the signed catalogue"_nx,[detail](ProgressBox*p){return nexus::catalog::Update(p,*detail);},[detail](Result result){Message(*detail,result);});
    },"Download verified guidance; offline repairs continue using the saved or bundled copy."_nx);
    menu->Add<SidebarEntryCallback>("Report a problem"_nx,[]{nexus::reports::DisplayReportMenu();});
    App::Push(std::move(menu));
}
}
void DisplayGuidedUsbRepair(u64 application){
    if(App::IsApplet()){Message("Open Nexus Manager in full-memory mode (hold R while starting a game), not through Album, then try again."_nx);return;}
    auto plan=std::make_shared<RecoveryPlan>();auto guide=std::make_shared<nexus::catalog::Loaded>();
    App::Push<ui::ProgressBox>(0,"Checking USB games"_nx,"Preparing a preview. No game files or links will be changed."_nx,[plan,guide,application](ui::ProgressBox*p){
        const auto catalogue=nexus::catalog::Load(*guide);if(R_FAILED(catalogue))nexus::Log("RC1-catalog-unavailable",catalogue);
        return ScanUsbRecovery(p,*plan,application);
    },[plan,guide](Result rc){Preview(plan,guide,rc);},ui::ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
}
