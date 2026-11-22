#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/manager_tools_menu.hpp"
#include "ui/nexus_violet.hpp"
#include "ui/nexus_dialog.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/file_manager_layout.hpp"
#include "app.hpp"
#include "utils/utils.hpp"
#include "utils/devoptab.hpp"
#include <algorithm>
namespace sphaira::ui::menu::managertools {
namespace {
namespace view=fileview;
struct FileControl {view::FileAction action;Button button;const char* label;};
constexpr FileControl FileControls[]={
    {view::FileAction::Copy,Button::X,"Copy"_nx},
    {view::FileAction::Move,Button::Y,"Move"_nx},
    {view::FileAction::Delete,Button::R2,"Delete"_nx},
    {view::FileAction::Paste,Button::L,"Paste here"_nx},
};
static_assert(std::size(FileControls)==view::ActionCount);
void Text(NVGcontext* vg,float x,float y,float size,NVGcolor c,const std::string& text,int align=NVG_ALIGN_LEFT|NVG_ALIGN_TOP){gfx::drawText(vg,x,y,size,c,text.c_str(),align);}
bool FullMemory(){
    if(!App::IsApplet())return true;
    App::Push<OptionBox>("Open Nexus Manager in full-memory mode (hold R while starting a game), or use its HOME app. File and save tools are unavailable in Album mode."_nx,"Close"_nx,[](auto){});return false;
}
void ChooseRestore(const nexusmanager::Save& save){
    auto folders=std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0,"Finding matching backups"_nx,save.name,[save,folders](ProgressBox* p){return nexusmanager::ListSaveBackups(p,save,*folders);},[save,folders](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not list backups. Finish PC transfer first. No save was changed."_nx);return;}
        if(folders->empty()){App::Push<OptionBox>("No complete Nexus version 2 backup matches this game and user.\n\nUpload the whole backup folder using PC transfer > Upload save backups, then disconnect. Older Nexus backups and DBI formats are not accepted for restore."_nx,"Close"_nx,[](auto){});return;}
        auto choices=std::make_unique<Sidebar>("Restore backup"_nx,save.name,Sidebar::Side::RIGHT);
        for(const auto& folder:*folders)choices->Add<SidebarEntryCallback>(folder.substr(folder.find_last_of('/')+1),[save,folder]{
            App::Push<OptionBox>(nexusmanager::SaveDetails(save)+"\n\nReplace this save with:\n"_nx+folder+"\n\nClose the game first. All backup files will be checked. The current save will be backed up before any replacement. Keep power and SD connected until it finishes."_nx,"Cancel"_nx,"Restore"_nx,0,[save,folder](auto answer){
                if(!answer || *answer!=1)return;auto detail=std::make_shared<std::string>();
                App::Push<ProgressBox>(0,"Restore save"_nx,save.name,[save,folder,detail](ProgressBox* p){const auto rc=nexusmanager::RestoreSave(p,save,folder,*detail);nexus::Log("manager-save-restore",rc);return rc;},[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close",[](auto){});});
            });
        },true);
        App::Push(std::move(choices));
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
}
Menu::Menu(Mode initial,const std::string& folder):MenuBase("Nexus Manager",MenuFlag_None),mode(initial),pending(initial==Mode::Saves || initial==Mode::Files),path(folder){
    SetupActions();
    SetAction(Button::UP,Action{[this]{if(selected)--selected;}});
    SetAction(Button::DOWN,Action{[this]{if(selected+1<Count())++selected;}});
    SetAction(Button::LEFT,Action{[this]{const auto rows=view::PageRows(mode==Mode::Files);selected=selected>=rows?selected-rows:0;}});
    SetAction(Button::RIGHT,Action{[this]{if(Count())selected=std::min(selected+view::PageRows(mode==Mode::Files),Count()-1);}});
}
void Menu::SetupActions(){
    actions_dirty=false;
    for(auto button:{Button::X,Button::Y,Button::R2,Button::L,Button::R,Button::SELECT})RemoveAction(button);
    SetAction(Button::A,Action{"Open"_nx,[this]{Open();}});
    SetAction(Button::B,Action{mode==Mode::Files?"Up"_nx:"Back"_nx,[this]{Back();}});
    SetAction(Button::START,Action{"Exit"_nx,App::Exit});
    if(mode==Mode::Saves){SetAction(Button::X,Action{"Refresh"_nx,[this]{Refresh();}});return;}
    SetAction(Button::R,Action{"Storage"_nx,[this]{Storage();}});
    SetAction(Button::SELECT,Action{"More"_nx,[this]{FileActions();}});
    if(mode==Mode::Files){
        // These shortcuts are labelled on the touch toolbar, leaving room in
        // the footer for navigation. Both input methods use the same handler.
        for(const auto& control:FileControls)SetAction(control.button,Action{[this,action=control.action]{RunFileAction(action);}});
    }
}
bool Menu::FileActionEnabled(view::FileAction action) const {
    if(mode!=Mode::Files)return false;
    if(action==view::FileAction::Paste)return clipboard && nexus::files::SafeDirectory(path);
    if(selected>=files.size())return false;
    return nexus::files::Safe(files[selected].path) ||
        (action==view::FileAction::Delete && nexus::files::StagePath(files[selected].path));
}
void Menu::RunFileAction(view::FileAction action){
    if(mode!=Mode::Files || !FullMemory())return;
    if(!FileActionEnabled(action)){
        const char* reason=action==view::FileAction::Paste && !clipboard?
            "Select a file or folder, then choose Copy or Move. Open the destination folder and choose Paste here. Use R Storage to switch between SD card and USB."_nx:
            action!=view::FileAction::Paste && selected>=files.size()?"Select a file or folder first."_nx:
            "This item or folder is managed game or system data and is read only here. Use Installed games to move or delete installed games. Choose an ordinary file or folder for File manager actions."_nx;
        App::Push<OptionBox>(reason,"Close"_nx,[](auto){});return;
    }
    switch(action){
        case view::FileAction::Copy:SelectClipboard(files[selected],false);break;
        case view::FileAction::Move:SelectClipboard(files[selected],true);break;
        case view::FileAction::Delete:Remove(files[selected]);break;
        case view::FileAction::Paste:Paste();break;
    }
}
size_t Menu::Count() const {return mode==Mode::Volumes?3:mode==Mode::Saves?saves.size():files.size();}
void Menu::Back(){
    actions_dirty=true;focus_path.clear();
    if(mode==Mode::Files){if(path=="/"){files.clear();generation=0;mode=Mode::Volumes;selected=0;}
        else {const auto slash=path.find_last_of('/');path=slash?path.substr(0,slash):"/";Refresh();}}
    else SetPop();
}
void Menu::Refresh(){
    actions_dirty=true;
    const auto keep=focus_path.empty() && mode==Mode::Files && selected<files.size()?files[selected].path:focus_path;
    focus_path.clear();selected=0;
    if(mode==Mode::Saves){
        auto rows=std::make_shared<std::vector<nexusmanager::Save>>();
        App::Push<ProgressBox>(0,"Finding game saves"_nx,"Read-only inventory"_nx,[rows](ProgressBox* p){return nexusmanager::ListSaves(p,*rows);},[this,rows](Result rc){
            saves.clear();if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not list game saves. No saves were changed."_nx);return;}saves=std::move(*rows);
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }else if(mode==Mode::Files){
        const auto source=usb;const auto folder=path;
        auto rows=std::make_shared<std::vector<nexusapp::Item>>();auto gen=std::make_shared<u64>(generation);
        App::Push<ProgressBox>(0,"Reading folder"_nx,(source?"USB:":"SD:")+folder,[rows,gen,source,folder](ProgressBox* p)->Result{
            return nexusfiles::ListFiles(p,source,folder,*rows,*gen);
        },[this,rows,gen,keep](Result rc){
            actions_dirty=true;
            files.clear();if(R_FAILED(rc)){generation=0;path="/";mode=Mode::Volumes;App::PushErrorBox(rc,"Could not read this storage. Reconnect it, then choose it again. No files were changed."_nx);return;}
            files=std::move(*rows);generation=*gen;
            const auto previous=std::find_if(files.begin(),files.end(),[&](const auto& item){return item.path==keep;});
            selected=previous==files.end()?0:size_t(previous-files.begin());
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }else GetPolledData(true);
}
void Menu::Inspect(const nexusapp::Item& item,bool hash){
    const auto source=usb;const auto gen=generation;auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,hash?"File checksum"_nx:"Read-only file preview"_nx,nexusmanager::Display(item.name),[item,source,gen,hash,detail](ProgressBox* p){
        return nexusmanager::InspectFile(p,source,gen,item,hash,*detail);
    },[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close"_nx,[](auto){});},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::Storage(){
    if(mode==Mode::Saves || !FullMemory())return;
    auto choices=std::make_unique<Sidebar>("Choose storage"_nx,clipboard?"Your clipboard selection is kept"_nx:"File manager"_nx,Sidebar::Side::RIGHT);
    for(bool target:{false,true})choices->Add<SidebarEntryCallback>(target?"USB drive"_nx:"SD card"_nx,[this,target]{
        usb=target;generation=0;path="/";files.clear();selected=0;focus_path.clear();mode=Mode::Files;Refresh();
    });
    choices->Add<SidebarEntryCallback>("Network locations"_nx,[]{devoptab::DisplayNetworkLocations();},"Browse SMB/NFS shares and copy verified files to USB."_nx);
    if(clipboard)choices->Add<SidebarEntryCallback>("Clear clipboard"_nx,[this]{ClearClipboard();});
    App::Push(std::move(choices));
}
void Menu::ClearClipboard(){clipboard.reset();cut=false;actions_dirty=true;App::Notify("Clipboard cleared. No files were changed."_nx);}
void Menu::SelectClipboard(const nexusapp::Item& item,bool move){
    clipboard=nexusfiles::Selection{usb,generation,item};cut=move;actions_dirty=true;
    App::Notify(std::string(move?"Ready to move: "_nx:"Ready to copy: "_nx)+nexusmanager::Display(item.name)+". Open the destination, then Paste here. R Storage changes drive."_nx);
}
void Menu::FileActions(){
    if(mode==Mode::Saves || !FullMemory())return;
    const bool item_selected=mode==Mode::Files && selected<files.size();
    const auto item=item_selected?files[selected]:nexusapp::Item{};
    const bool editable=item_selected && nexus::files::Safe(item.path);
    const bool retained_stage=item_selected && nexus::files::StagePath(item.path);
    const auto subtitle=!item_selected?"Storage and clipboard"_nx:retained_stage?"Temporary transfer item"_nx:!editable?"Managed data / read only"_nx:item.directory?"Folder actions"_nx:"File actions"_nx;
    auto choices=std::make_unique<Sidebar>(item_selected?nexusmanager::Display(item.name):"File manager"_nx,subtitle,Sidebar::Side::RIGHT);
    if(item_selected){
        if(editable){
            choices->Add<SidebarEntryCallback>("Copy"_nx,[this,item]{SelectClipboard(item,false);});
            choices->Add<SidebarEntryCallback>("Move"_nx,[this,item]{SelectClipboard(item,true);});
        }
        if(editable || retained_stage)choices->Add<SidebarEntryCallback>("Delete"_nx,[this,item]{Remove(item);});
        const auto source=usb;
        choices->Add<SidebarEntryCallback>("Details"_nx,[item,source,editable,retained_stage]{
            const auto detail=std::string(source?"USB:":"SD:")+nexusmanager::Display(item.path)+"\n\n"+
                (item.directory?std::string("Folder."_nx):"Size: "_nx+utils::formatSizeStorage(item.size))+"\n\n"+
                (retained_stage?"Incomplete transfer item. Delete reviews its exact contents before removal. It cannot be copied or moved."_nx:
                    editable?"Files and subfolders are counted before copying, moving or deleting."_nx:"Managed game or system data is read only here. Use Installed games to manage installed games."_nx);
            App::Push<OptionBox>(detail,"Close"_nx,[](auto){});
        });
        if(!item.directory){
            choices->Add<SidebarEntryCallback>("Preview first 256 bytes"_nx,[this,item]{Inspect(item,false);});
            choices->Add<SidebarEntryCallback>("Calculate SHA-256"_nx,[this,item]{App::Push<OptionBox>("Read the whole file to calculate its SHA-256 checksum? Large files can take time. No file is changed."_nx,"Cancel"_nx,"Calculate"_nx,0,[this,item](auto answer){if(answer && *answer==1)Inspect(item,true);});});
        }
    }
    if(clipboard && mode==Mode::Files && nexus::files::SafeDirectory(path))choices->Add<SidebarEntryCallback>("Paste here"_nx,[this]{Paste();});
    if(clipboard)choices->Add<SidebarEntryCallback>("Clear clipboard"_nx,[this]{ClearClipboard();});
    choices->Add<SidebarEntryCallback>(mode==Mode::Files?"Refresh folder"_nx:"Refresh storage"_nx,[this]{Refresh();});
    App::Push(std::move(choices));
}
void Menu::Paste(){
    if(mode!=Mode::Files || !clipboard || !FullMemory())return;
    if(!nexus::files::SafeDirectory(path)){App::Push<OptionBox>("This folder contains managed game or system data and is read only here. Choose another destination, or use Installed games for installed games."_nx,"Close"_nx,[](auto){});return;}
    const auto source=*clipboard;const auto destination_usb=usb;const auto destination_generation=generation;
    const auto folder=path;const bool move=cut;
    auto plan=std::make_shared<nexusfiles::Plan>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,move?"Preparing move"_nx:"Preparing copy"_nx,nexusmanager::Display(source.item.name),[source,destination_usb,destination_generation,folder,move,plan,detail](ProgressBox* p){
        return nexusfiles::PrepareTransfer(p,source,destination_usb,destination_generation,folder,move,*plan,*detail);
    },[this,plan,detail,move](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"Could not prepare this transfer. No files were changed."_nx:*detail);return;}
        const auto review=nexusfiles::TransferPreview(*plan)+"\n\nExisting destination files are never overwritten. Keep storage connected."_nx;
        App::Push<OptionBox>(review,"Cancel"_nx,move?"Move here"_nx:"Copy here"_nx,0,[this,plan,move](auto answer){
            if(!answer || *answer!=1)return;
            auto report=std::make_shared<nexusfiles::Report>();
            App::Push<ProgressBox>(0,move?"Moving files"_nx:"Copying files"_nx,nexusmanager::Display(plan->source.item.name),[plan,report](ProgressBox* p){
                return nexusfiles::Transfer(p,*plan,*report);
            },[this,plan,report,move](Result result){
                if(view::ClearCutAfterTransfer(move,R_SUCCEEDED(result),report->complete,report->removed,report->removal_started)){clipboard.reset();cut=false;}
                actions_dirty=true;pending=true;
                if(report->published)focus_path=plan->final_path;
                const auto message=report->detail.empty()?(R_SUCCEEDED(result)?"Transfer complete."_nx:"Transfer stopped. Check the storage connection before trying again."_nx):report->detail;
                if(R_FAILED(result))App::PushErrorBox(result,message);else App::Push<OptionBox>(message,"Done"_nx,[](auto){});
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::Remove(const nexusapp::Item& item){
    if(mode!=Mode::Files||!FullMemory())return;
    const nexusfiles::Selection source{usb,generation,item};
    const auto text=std::string("Permanently delete from "_nx)+(usb?"USB":"SD card"_nx)+"?\n\n"+item.path+
        "\n\nThe selected item and everything inside it will be removed, including any saves stored there. This cannot be undone."_nx;
    App::Push<OptionBox>(text,"Cancel"_nx,"Delete"_nx,0,[this,source](auto answer){
        if(!answer||*answer!=1)return;auto report=std::make_shared<nexusfiles::Report>();
        App::Push<ProgressBox>(0,"Deleting files"_nx,nexusmanager::Display(source.item.name),[source,report](ProgressBox* p)->Result{
            nexusfiles::DeletePlan plan;std::string detail;const auto rc=nexusfiles::PrepareDelete(p,source,plan,detail);
            if(R_FAILED(rc)){report->detail=detail;return rc;}return nexusfiles::Delete(p,plan,*report);
        },[this,source,report](Result rc){
            if(clipboard&&(report->removal_started||report->removed||report->complete)&&clipboard->usb==source.usb&&
                clipboard->generation==source.generation&&view::IsSameOrChild(clipboard->item.path,source.item.path)){clipboard.reset();cut=false;}
            actions_dirty=true;pending=true;
            const auto text=report->detail.empty()?"Deletion stopped. Refresh before retrying."_nx:report->detail;
            if(R_FAILED(rc))App::PushErrorBox(rc,text);else App::Push<OptionBox>(text,"Done"_nx,[](auto){});
        },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
    });
}
void Menu::Open(){
    if(selected>=Count())return;
    if(!FullMemory())return;
    if(mode==Mode::Volumes){if(selected==2){devoptab::DisplayNetworkLocations();return;}usb=selected==1;generation=0;path="/";mode=Mode::Files;Refresh();return;}
    if(mode==Mode::Files){
        const auto item=files[selected];
        if(item.directory){path=item.path;Refresh();return;}
        FileActions();return;
    }
    const auto save=saves[selected];
    auto choices=std::make_unique<Sidebar>(save.name,"Save backup and restore"_nx,Sidebar::Side::RIGHT);
    choices->Add<SidebarEntryCallback>("Back up save"_nx,[save]{App::Push<OptionBox>(nexusmanager::SaveDetails(save)+"\n\nBack up this save to SD? The original is kept. Close the game first. Every copied file is verified."_nx,"Cancel"_nx,"Back up"_nx,0,[save](auto answer){
        if(!answer || *answer!=1)return;
        auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Save backup"_nx,save.name,[save,detail](ProgressBox* p){
            const auto rc=nexusmanager::BackupSave(p,save,*detail);nexus::Log("manager-save-backup",rc);return rc;
        },[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close"_nx,[](auto){});},ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
    });},true);
    choices->Add<SidebarEntryCallback>("Restore a backup"_nx,[save]{ChooseRestore(save);},true);
    App::Push(std::move(choices));
}
void Menu::Update(Controller* c,TouchInfo* touch){
    if(actions_dirty)SetupActions();
    MenuBase::Update(c,touch);
    if(pending && HasFocus()){pending=false;if(!App::IsApplet())Refresh();}
    if(HasFocus() && touch->is_clicked){
        if(mode==Mode::Files)for(size_t i=0;i<view::ActionCount;++i){
            const auto button=view::ActionButton(i);
            if(touch->in_range({button.x,button.y,button.w,button.h})){RunFileAction(FileControls[i].action);break;}
        }
        const auto rows=view::PageRows(mode==Mode::Files),begin=selected/rows*rows;
        for(size_t i=begin;HasFocus() && i<std::min(Count(),begin+rows);++i){const auto row=view::Row(i,mode==Mode::Files);if(touch->in_range({row.x,row.y,row.w,row.h})){
            if(selected==i)Open();else selected=i;break;
        }}
    }
    SetSubHeading(std::to_string(Count()?selected+1:0)+" / "+std::to_string(Count()));
}
void Menu::Draw(NVGcontext* vg,Theme* theme){
    MenuBase::Draw(vg,theme);violet::Card(vg,{42,106,1196,533},false);
    const auto fg=nexusdialog::Text(),muted=nexusdialog::Text(),accent=violet::Accent();
    const std::string heading=mode==Mode::Volumes?"File manager"_nx:mode==Mode::Saves?"Save backups"_nx:(usb?"File manager / USB"_nx:"File manager / SD card"_nx);
    Text(vg,62,114,27,fg,heading);
    nvgSave(vg);nvgScissor(vg,62,147,1150,23);
    Text(vg,62,147,15,muted,mode==Mode::Files?std::string(usb?"USB:":"SD:")+nexusmanager::Display(path)+(nexus::files::SafeDirectory(path)?"":"  /  Managed data: read only"_nx):mode==Mode::Saves?"Choose a game and user / back up or restore with confirmation"_nx:"Copy or Move an item, choose its destination, then Paste here. Folders include all their contents."_nx);nvgRestore(vg);
    if(mode==Mode::Files)for(size_t i=0;i<view::ActionCount;++i){
        const auto& control=FileControls[i];const auto button=view::ActionButton(i);
        const auto label=std::string(gfx::getButton(control.button))+"  "+control.label;
        const bool enabled=FileActionEnabled(control.action);
        // Keep labels white even when an action is unavailable; pressing it
        // explains what is needed. A ready clipboard highlights Paste here.
        nexusdialog::Button(vg,{button.x,button.y,button.w,button.h},label.c_str(),enabled && control.action==view::FileAction::Paste);
    }
    const auto rows=view::PageRows(mode==Mode::Files),begin=selected/rows*rows;
    for(size_t i=begin;i<std::min(Count(),begin+rows);++i){
        const auto row=view::Row(i,mode==Mode::Files);const float y=row.y;violet::Card(vg,{row.x,row.y,row.w,row.h},i==selected,true);
        std::string label,right;
        if(mode==Mode::Volumes){label=i==2?"Network locations"_nx:i?"USB drive"_nx:"SD card"_nx;right=i==2?"SMB / NFS":"Open"_nx;}
        else if(mode==Mode::Saves){label=saves[i].name;right=saves[i].info.save_data_type==FsSaveDataType_Account?"User save"_nx:"Device save"_nx;}
        else {label=nexusmanager::Display(files[i].name);right=files[i].directory?"Folder"_nx:utils::formatSizeStorage(files[i].size);}
        if(mode==Mode::Files && files[i].directory){
            gfx::drawRect(vg,72,y+19,29,21,nvgRGBA(110,157,244,100),3);
            gfx::drawRect(vg,72,y+14,14,9,accent,2);
        }
        nvgSave(vg);nvgScissor(vg,114,y+4,820,50);Text(vg,114,y+17,20,fg,label);nvgRestore(vg);
        Text(vg,1208,y+19,16,fg,right,NVG_ALIGN_RIGHT|NVG_ALIGN_TOP);
    }
    if(!Count())Text(vg,72,mode==Mode::Files?262:205,20,muted,App::IsApplet()?"Use full-memory mode to access these tools."_nx:mode==Mode::Saves?"No supported primary user/device saves found."_nx:!nexus::files::SafeDirectory(path)?"Managed game or system data is read only here."_nx:clipboard?"This folder is empty. Paste here adds your selected item."_nx:"This folder is empty. Use Storage to choose another drive."_nx);
    const auto footer=view::ClipboardLine();nvgSave(vg);nvgScissor(vg,footer.x,footer.y,footer.w,footer.h);
    const auto hint=mode==Mode::Saves?std::string("Backups stay on SD. Keep a separate copy on your PC."_nx):
        clipboard?std::string(cut?"Ready to move: "_nx:"Ready to copy: "_nx)+(clipboard->usb?"USB:":"SD:")+nexusmanager::Display(clipboard->item.path):
        "Select an item, then choose Copy, Move or Delete. R Storage changes drive. Left / right: page."_nx;
    Text(vg,footer.x,footer.y+3,17,muted,hint);nvgRestore(vg);
}
}
