#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_import.hpp"
#include "nexus/usb_library_startup.hpp"
#include "nexus/usb_ticket_prepare.hpp"
#include "nexus/progress_safety.hpp"
#include "app.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nexus_dialog.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/usb_import_view.hpp"
#include <algorithm>
#include <functional>
#include <memory>

namespace sphaira::nexusapp {
namespace {
using namespace ui;
namespace view=ui::usbimportview;
std::string Diagnostic(Result rc){char text[40]{};snprintf(text,sizeof(text),"Diagnostic code: 0x%08X"_nx,rc);return text;}
std::string Failure(const char* heading,const std::string& detail,Result rc,const char* fallback){
    auto text=std::string(heading)+"\n\n"+(detail.empty()?fallback:detail);
    if(R_FAILED(rc))text+="\n\n"+Diagnostic(rc);
    return text;
}
struct ReviewRow {std::string title,subtitle,detail;};
void DrawLine(NVGcontext* vg,float x,float y,float size,const NVGcolor& colour,const std::string& text,float width){
    nvgFontSize(vg,size);
    const auto fitted=view::FitLineText(text,width,[&](std::string_view value){return nvgTextBounds(vg,0,0,value.data(),value.data()+value.size(),nullptr);});
    gfx::drawText(vg,x,y,size,colour,fitted.c_str());
}

// One browse surface: each row opens one existing, scrollable detail panel.
class ReviewMenu final:public ui::menu::MenuBase {
    static constexpr size_t PageRows=5;
    std::string heading,summary,note;
    std::vector<ReviewRow> rows;
    size_t selected{};
    std::string primary_label;
    std::function<void()> primary;
    static Vec4 Row(size_t index){return {62,222+float(index%PageRows)*74,1156,66};}
    bool Active() const{return HasFocus()&&!ShouldPop();}
    void Open(){if(Active()&&selected<rows.size())App::Push<OptionBox>(rows[selected].detail,"Back"_nx,[](auto){});}
public:
    ReviewMenu(std::string title,std::string counts,std::string guidance,std::vector<ReviewRow> items)
        :MenuBase("USB games"_nx,ui::menu::MenuFlag_None),heading(std::move(title)),summary(std::move(counts)),note(std::move(guidance)),rows(std::move(items)){
        SetAction(Button::A,Action{"Details"_nx,[this]{Open();}});
        SetAction(Button::B,Action{"Back"_nx,[this]{if(Active())SetPop();}});
        SetAction(Button::UP,Action{[this]{if(Active()&&selected)--selected;}});
        SetAction(Button::DOWN,Action{[this]{if(Active()&&selected+1<rows.size())++selected;}});
        SetAction(Button::LEFT,Action{[this]{if(Active())selected=selected>=PageRows?selected-PageRows:0;}});
        SetAction(Button::RIGHT,Action{[this]{if(Active()&&!rows.empty())selected=std::min(selected+PageRows,rows.size()-1);}});
    }
    const char* GetShortTitle() const override{return "USB games"_nx;}
    void SetPrimary(std::string label,std::function<void()> callback){
        primary_label=std::move(label);primary=std::move(callback);
        SetAction(Button::X,Action{primary_label,[this]{if(Active()&&primary)primary();}});
    }
    void SetSecondary(const std::string& label,std::function<void()> callback){SetAction(Button::Y,Action{label,[this,callback=std::move(callback)]{if(Active())callback();}});}
    void Update(Controller* controller,TouchInfo* touch) override{
        if(!Active())return;
        MenuBase::Update(controller,touch);
        if(!Active())return;
        if(touch->is_clicked){
            if(primary&&touch->in_range({888,116,330,88})){primary();return;}
            const auto first=selected/PageRows*PageRows;
            for(size_t i=first;i<std::min(rows.size(),first+PageRows);++i){
                if(touch->in_range(Row(i))){if(selected==i)Open();else selected=i;break;}
            }
        }
        SetSubHeading(std::to_string(rows.empty()?0:selected+1)+" / "+std::to_string(rows.size())+"  |  Left / right: page"_nx);
    }
    void Draw(NVGcontext* vg,Theme* theme) override{
        MenuBase::Draw(vg,theme);
        violet::Card(vg,{42,106,1196,533},false);
        const auto white=nexusdialog::Text();
        const float header_width=primary?800:1140;
        nvgSave(vg);nvgIntersectScissor(vg,62,115,header_width,96);
        DrawLine(vg,62,117,27,white,heading,header_width);
        DrawLine(vg,62,155,19,white,summary,header_width);
        DrawLine(vg,62,184,16,white,note,header_width);nvgRestore(vg);
        if(primary){
            const auto label=std::string(gfx::getButton(Button::X))+"  "+primary_label;
            nexusdialog::Button(vg,{888,116,330,88},label.c_str(),true);
        }
        const auto first=selected/PageRows*PageRows;
        for(size_t i=first;i<std::min(rows.size(),first+PageRows);++i){
            const auto box=Row(i);violet::Card(vg,box,i==selected,true);
            nvgSave(vg);nvgIntersectScissor(vg,box.x+16,box.y+7,box.w-38,box.h-12);
            DrawLine(vg,box.x+16,box.y+9,22,white,rows[i].title,box.w-38);
            DrawLine(vg,box.x+16,box.y+38,16,white,rows[i].subtitle,box.w-38);nvgRestore(vg);
        }
        const auto hint=std::string(gfx::getButton(Button::A))+"  Full details and long reports  /  "_nx+gfx::getButton(Button::UP)+" "+gfx::getButton(Button::DOWN)+"  Choose an item"_nx;
        gfx::drawText(vg,66,613,16,white,hint.c_str());
    }
};

view::Kind Kind(unsigned type){
    if(type==NcmContentMetaType_Application)return view::Kind::Game;
    if(type==NcmContentMetaType_Patch)return view::Kind::Update;
    if(type==NcmContentMetaType_AddOnContent)return view::Kind::AddOn;
    if(type==NcmContentMetaType_DataPatch)return view::Kind::AddOnUpdate;
    return view::Kind::Unknown;
}
view::Preview CountPreview(const UsbImportScan& scan,const UsbLibraryPlan& plan){
    std::vector<view::Item> items;
    for(size_t i=0;i<scan.titles.size();++i){
        const auto& row=scan.titles[i];
        view::State state=view::State::Incomplete;
        switch(row.status){case UsbImportStatus::Ready:state=view::State::Ready;break;
            case UsbImportStatus::AlreadyRegistered:state=view::State::AlreadyHere;break;
            case UsbImportStatus::TicketMissing:state=view::State::MissingPermission;break;
            case UsbImportStatus::Incomplete:break;}
        if(i<plan.family_reason.size()&&!plan.family_reason[i].empty()&&state!=view::State::MissingPermission)state=view::State::Incomplete;
        items.push_back({row.record.key.id,row.record.application_id,row.record.key.version,unsigned(row.record.key.type),state,Kind(row.record.key.type),row.name});
    }
    // Selection belongs to the backend's complete-family plan. In particular,
    // already-healthy USB siblings can be included with the member needing repair.
    return view::Build(items,&plan.selected);
}
const char* FriendlyStatus(UsbImportStatus status,bool selected){
    switch(status){case UsbImportStatus::Ready:return selected?"Ready to repair"_nx:"Not selected"_nx;
        case UsbImportStatus::AlreadyRegistered:return selected?"Included in family repair"_nx:"Already on this system"_nx;
        case UsbImportStatus::Incomplete:return "Needs checking"_nx;
        case UsbImportStatus::TicketMissing:return "Ticket not found in system"_nx;}
    return "Needs checking"_nx;
}
std::string ItemDetails(const UsbImportTitle& item,bool selected,const std::string& family_reason){
    char id[32]{};snprintf(id,sizeof(id),"%016llX",static_cast<unsigned long long>(item.record.key.id));
    std::string text=item.name+"\n"+view::KindName(Kind(item.record.key.type))+" / "+FriendlyStatus(item.status,selected)+"\n\n";
    switch(item.status){
        case UsbImportStatus::Ready:text+=selected?"Ready to verify and repair with this game's USB updates and DLC. The existing USB files will be reused."_nx:family_reason.empty()?"This item is not part of the current repair plan. A newer version or a native registration can take priority."_nx:"This item is ready individually, but its whole game family must pass the checks before any member is repaired."_nx;break;
        case UsbImportStatus::AlreadyRegistered:text+=selected?"This USB item is already healthy. It is included so the base game, updates and DLC stay together while this family is repaired."_nx:"This item is already registered on this system. Its current registration will be kept."_nx;break;
        case UsbImportStatus::Incomplete:text+="This item cannot be added safely yet. Read the check below before retrying."_nx;break;
        case UsbImportStatus::TicketMissing:text+="The required ticket was not found in the current system ticket database. USB stores only optional backup copies. This check does not prove a working game is broken. If the game will not launch, use Prepare USB on the original system or reinstall from your original backup. Do not delete a working game because of this result."_nx;break;
    }
    if(!family_reason.empty())text+="\n\nGame family not selected: "_nx+family_reason;
    if(!item.reason.empty())text+="\n\nCheck: "+item.reason;
    text+="\n\nTitle ID: "_nx+std::string(id)+"\nVersion: "_nx+std::to_string(item.record.key.version);
    if(!item.meta_path.empty())text+="\nMetadata file: "_nx+item.meta_path;
    return text;
}
void ResultPage(std::shared_ptr<UsbImportResult> result,Result rc);
void RestoreBackup(ReviewMenu* owner){
    auto consumed=std::make_shared<bool>();
    App::Push<OptionBox>("Restore the saved USB import backup?\n\nThis restores the interrupted game's registration and routing. USB game files and saves stay in place. Any game permissions already added remain installed.\n\nKeep the SD and USB connected. Fully restart the Switch after restoration."_nx,"Cancel"_nx,"Restore backup"_nx,0,[owner,consumed](auto choice){
        if(*consumed)return;*consumed=true;
        if(!choice||*choice!=1||owner->ShouldPop())return;
        owner->SetPop();auto result=std::make_shared<UsbImportResult>();
        App::Push<ProgressBox>(0,"Restore import backup"_nx,"Restoring previous game registration"_nx,[result](ProgressBox* p){return RestoreUsbImport(p,*result);},[result](Result rc){
            if(R_FAILED(rc)&&result->detail.empty())result->detail="Restoration could not finish. Keep the backup folders and avoid further installs until recovery completes."_nx;
            ResultPage(result,rc);
        },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
    });
}
void ResultPage(std::shared_ptr<UsbImportResult> result,Result rc){
    const auto heading=result->pending?"Recovery needed"_nx:result->rolled_back?"Previous registration restored"_nx:R_FAILED(rc)?"Adding games stopped"_nx:"USB import complete"_nx;
    const auto summary="Added: "+std::to_string(result->imported)+" items  /  Not added: "_nx+std::to_string(result->skipped)+" items"_nx;
    const auto note=result->pending?"Keep the backups. Restore them before installing or moving anything."_nx:result->restart?"Fully restart the Switch before playing or making more changes."_nx:"Choose a result below to see exactly what happened."_nx;
    std::string detail=result->detail;
    if(R_FAILED(rc))detail+="\n\n"+Diagnostic(rc);
    detail+="\n\n"+summary;
    if(result->rolled_back&&result->imported)detail+="\nThe interrupted game's changes were restored. Earlier completed imports are kept."_nx;
    detail+="\n\nGame files and saves were not copied or deleted. Game permissions already added remain installed after a rollback."_nx;
    if(!result->report_path.empty())detail+="\n\nSaved report:\n"_nx+result->report_path;
    std::vector<ReviewRow> rows{{"Summary and saved report"_nx,note,detail}};
    for(size_t i=0;i<result->entries.size();++i)rows.push_back({view::OutcomeTitle(result->entries[i]),"Outcome "_nx+std::to_string(i+1)+" / "+std::to_string(result->entries.size()),result->entries[i]});
    auto menu=std::make_unique<ReviewMenu>(heading,summary,note,std::move(rows));
    menu->SetAction(Button::B,Action{"Done"_nx,[ptr=menu.get()]{if(ptr->HasFocus()&&!ptr->ShouldPop())ptr->SetPop();}});
    if(result->pending)menu->SetPrimary("Restore backup"_nx,[ptr=menu.get()]{RestoreBackup(ptr);});
    else if(result->restart)menu->SetPrimary("Exit Manager"_nx,[]{App::Exit();});
    App::Push(std::move(menu));
}
void PreparationReport(std::shared_ptr<UsbTicketPrepareResult> result,Result rc){
    const auto heading=R_FAILED(rc)?"Preparation stopped"_nx:"USB preparation complete"_nx;
    const auto summary="Prepared: "_nx+std::to_string(result->prepared)+"  /  Already prepared: "_nx+std::to_string(result->already_prepared)+"  /  Need backup: "_nx+std::to_string(result->needs_backup);
    const auto note=R_FAILED(rc)?"Open the summary and outcomes before retrying."_nx:"Next: boot the new SD card/system and scan this USB again."_nx;
    std::string detail=result->detail+"\n\n"+summary+"\nSkipped: "_nx+std::to_string(result->skipped)+"\n\nGame files, saves and the current game list were not changed."_nx;
    if(R_FAILED(rc))detail+="\n\n"+Diagnostic(rc);
    if(result->needs_backup)detail+="\n\nItems without usable permissions must be reinstalled from your original backup."_nx;
    if(!result->report_path.empty())detail+="\n\nSaved report:\n"_nx+result->report_path;
    std::vector<ReviewRow> rows{{"Summary and saved report"_nx,note,detail}};
    for(size_t i=0;i<result->entries.size();++i)rows.push_back({view::OutcomeTitle(result->entries[i]),"Preparation outcome "_nx+std::to_string(i+1),result->entries[i]});
    App::Push<ReviewMenu>(heading,summary,note,std::move(rows));
}
void PrepareForAnotherCard(std::shared_ptr<UsbImportScan> scan){
    auto plan=std::make_shared<UsbTicketPreparePlan>();
    App::Push<ProgressBox>(0,"Check USB preparation"_nx,"Reading permissions from this system; no changes"_nx,[scan,plan](ProgressBox* p){return PlanUsbTicketPreparation(p,*scan,*plan);},[plan](Result rc){
        if(R_FAILED(rc)||!plan->complete){App::Push<OptionBox>(Failure("Preparation check could not finish."_nx,plan->detail,rc,"No setup files were saved. Keep the original game backup and review the diagnostic before retrying."_nx),"Back"_nx,[](auto){});return;}
        std::vector<ReviewRow> rows{{"What preparation does"_nx,"Use the original SD card/system where these games work"_nx,UsbTicketPreparePreview(*plan)+"\n\nPreparation copies only supported game-permission files. It does not copy games or saves. Games that cannot be prepared need your original backup."_nx}};
        for(const auto& item:plan->titles)rows.push_back({view::OutcomeTitle(item.name),UsbTicketPrepareStatusText(item.status),item.name+"\n"+UsbTicketPrepareStatusText(item.status)+"\n\n"+item.reason+"\n\n"+item.identity});
        auto menu=std::make_unique<ReviewMenu>("Prepare USB for another SD card"_nx,std::to_string(plan->tickets_to_save)+" permission files can be saved"_nx,"Game files and saves stay where they are."_nx,std::move(rows));
        if(plan->tickets_to_save)menu->SetPrimary("Save preparation"_nx,[plan,ptr=menu.get()]{
            auto consumed=std::make_shared<bool>();
            App::Push<OptionBox>("Save "_nx+std::to_string(plan->tickets_to_save)+" supported permission files on USB?\n\nUse the original SD card/system where the games work. Existing copies are kept. Game files, saves and current registrations are unchanged.\n\nKeep the USB connected until it finishes."_nx,"Cancel"_nx,"Save on USB"_nx,0,[plan,ptr,consumed](auto choice){
                if(*consumed)return;*consumed=true;
                if(!choice||*choice!=1||ptr->ShouldPop())return;ptr->SetPop();auto result=std::make_shared<UsbTicketPrepareResult>();
                App::Push<ProgressBox>(0,"Prepare USB"_nx,"Saving supported game permissions"_nx,[plan,result](ProgressBox* p){return PrepareUsbTickets(p,*plan,*result);},[result](Result rc){
                    if(R_FAILED(rc)&&result->detail.empty())result->detail="Preparation could not finish. Review the outcomes before retrying; some permission files may already have been saved."_nx;
                    PreparationReport(result,rc);
                },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
            });
        });
        App::Push(std::move(menu));
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void PreparationHelp(std::shared_ptr<UsbImportScan> scan){
    auto consumed=std::make_shared<bool>();
    App::Push<OptionBox>("Need permissions on a new SD card/system?\n\n1. Start the original SD card/system where these USB games work.\n2. Scan the USB here, then choose Prepare USB and save the supported permissions.\n3. Boot the new SD card/system, scan again and add the ready games.\n\nIf the original system is unavailable, or a permission cannot be saved, reinstall that game from your original backup. Games and saves are not copied by this tool."_nx,"Back"_nx,"Check preparation"_nx,0,[scan,consumed](auto choice){if(*consumed)return;*consumed=true;if(choice&&*choice==1)PrepareForAnotherCard(scan);});
}
void Preview(std::shared_ptr<UsbImportScan> scan){
    const auto plan=PlanUsbLibrary(*scan);
    const auto counts=CountPreview(*scan,plan);
    std::vector<ReviewRow> rows;
    const auto summary="Games ready: "_nx+std::to_string(plan.summary.repairable_games)+"  /  "+std::to_string(counts.ready)+" items to verify"_nx;
    const auto note="Need checking: "_nx+std::to_string(plan.summary.blocked_games)+"  /  "+view::ReadyBreakdown(counts);
    std::string details=summary+"\n\n"+note+"\n\nOther items: "_nx+view::OtherBreakdown(counts)+"\n\nBase games, updates and DLC are checked and repaired together as game families. Healthy USB members are included when another member needs repair. A missing or unsafe member blocks that family's changes. Native registrations are kept and newer installed versions are not downgraded. DLC items include DLC updates.\n\nYour USB game files and saves stay in place."_nx;
    if(!scan->detail.empty())details+="\n\nScan details:\n"_nx+scan->detail;
    if(scan->titles.empty()){
        details=(scan->nca_files||scan->unreadable_ncas?std::string("Content files are present, but the scan could not read any supported game details. The drive is not necessarily empty."_nx):std::string("No installed USB games were found."_nx))+"\n\nThis tool adds games already installed on USB. For NSP, NSZ, XCI or XCZ files, use Install games instead.\n\n"_nx+scan->detail;
    }else if(!counts.ready)details+=plan.summary.affected_games?"\n\nNo complete game family is ready to repair. Browse the items to see what they need, or use Prepare USB for missing permissions."_nx:"\n\nNo USB game family needs repair. Existing registrations will be kept."_nx;
    rows.push_back({scan->titles.empty()?"No installed USB games found"_nx:"Scan summary"_nx,scan->titles.empty()?"Open for the scan findings and next steps"_nx:note,details});
    for(const auto index:counts.order){
        const auto& item=scan->titles[index];
        const bool known=item.record.key.id&&Kind(item.record.key.type)!=view::Kind::Unknown;
        const auto reason=index<plan.family_reason.size()?plan.family_reason[index]:std::string{};
        const auto status=reason.empty()?FriendlyStatus(item.status,counts.selected[index]):"Game family needs checking"_nx;
        const auto subtitle=known?std::string(view::KindName(Kind(item.record.key.type)))+" / "+status+" / version "_nx+std::to_string(item.record.key.version):"Needs checking / open for the file and diagnostic"_nx;
        const auto family=counts.family[index].empty()?item.name:counts.family[index];
        rows.push_back({known?family:"Unrecognised content"_nx,subtitle,ItemDetails(item,counts.selected[index],reason)});
    }
    auto menu=std::make_unique<ReviewMenu>("Repair USB games"_nx,summary,note,std::move(rows));
    if(!scan->titles.empty())menu->SetSecondary("Prepare USB"_nx,[scan]{PreparationHelp(scan);});
    if(counts.ready&&plan.summary.repairable_games)menu->SetPrimary("Repair ready games"_nx,[scan,counts,families=plan.summary.repairable_games,ptr=menu.get()]{
        auto consumed=std::make_shared<bool>();
        App::Push<OptionBox>("Repair "_nx+std::to_string(families)+(families==1?" game?\n\n"_nx:" games?\n\n"_nx)+view::ReadyBreakdown(counts)+"\n\nNexus verifies the USB files and backs up this system's game records first. Each family's USB base game, updates and DLC are handled together, including healthy USB members. Native registrations are kept. Game files and saves stay in place. Blocked families are skipped and listed in the report.\n\nGame permissions added during repair remain installed after a rollback. Keep SD and USB connected, then fully restart the Switch afterward."_nx,"Cancel"_nx,"Repair games"_nx,0,[scan,ptr,consumed](auto choice){
            if(*consumed)return;*consumed=true;
            if(!choice||*choice!=1||ptr->ShouldPop())return;ptr->SetPop();auto result=std::make_shared<UsbImportResult>();
            App::Push<ProgressBox>(0,"Repair USB games"_nx,"Verifying and registering existing files"_nx,[scan,result](ProgressBox* p){return ImportUsbLibrary(p,*scan,*result);},[result](Result rc){
                if(R_FAILED(rc)&&result->detail.empty())result->detail="The import could not finish. Review the diagnostic and per-item outcomes before trying again."_nx;
                ResultPage(result,rc);
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    });
    App::Push(std::move(menu));
}
void ScanAndPreview(){
    auto scan=std::make_shared<UsbImportScan>();auto pending=std::make_shared<bool>();
    App::Push<ProgressBox>(0,"Scan USB games"_nx,"Checking previous setup, then reading installed games"_nx,[scan,pending](ProgressBox* p)->Result{
        R_TRY(p->ShouldExitResult());
        const auto checked=UsbImportPending(*pending);
        if(R_FAILED(checked)){scan->detail="Could not check for an interrupted import on this SD card. No library changes were made. Keep any existing import backup folders."_nx;return checked;}
        if(*pending)return 0;
        return ScanUsbImports(p,*scan);
    },[scan,pending](Result rc){
        if(*pending){
            auto menu=std::make_unique<ReviewMenu>("Finish recovery first"_nx,"An earlier USB import was interrupted"_nx,"Restore its backup before adding, installing or moving games."_nx,std::vector<ReviewRow>{{"Why recovery is needed"_nx,"Your USB game files and saves remain in place"_nx,"An earlier import has a saved recovery journal. Restore the previous registration and routing before making more changes. Keep all import backup folders.\n\nFully restart the Switch after restoration. Game permissions already installed remain installed."_nx}});
            menu->SetPrimary("Restore backup"_nx,[ptr=menu.get()]{RestoreBackup(ptr);});App::Push(std::move(menu));return;
        }
        if(R_FAILED(rc)||!scan->complete){App::Push<OptionBox>(Failure("The USB scan could not finish."_nx,scan->detail,rc,"No library changes were made. Review the diagnostic before retrying."_nx),"Back"_nx,[](auto){});return;}
        Preview(scan);
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
}
void DisplayUsbImport(){
    StopUsbLibraryStartupCheck();
    if(App::IsApplet()){App::Push<ui::OptionBox>("Open the Nexus Manager HOME app, or hold R while starting a game for full-memory mode, then scan USB again."_nx,"Back"_nx,[](auto){});return;}
    ScanAndPreview();
}
}
