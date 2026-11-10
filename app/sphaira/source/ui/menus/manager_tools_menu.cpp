// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/manager_tools_menu.hpp"
#include "ui/nexus_violet.hpp"
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
void Text(NVGcontext* vg,float x,float y,float size,NVGcolor c,const std::string& text,int align=NVG_ALIGN_LEFT|NVG_ALIGN_TOP){gfx::drawText(vg,x,y,size,c,text.c_str(),align);}
bool FullMemory(){
    if(!App::IsApplet())return true;
    App::Push<OptionBox>("Open Nexus Manager in full-memory mode (hold R while starting a game), or use its HOME app. File and save tools are unavailable in Album mode.","Close",[](auto){});return false;
}
void ChooseRestore(const nexusmanager::Save& save){
    auto folders=std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0,"Finding matching backups",save.name,[save,folders](ProgressBox* p){return nexusmanager::ListSaveBackups(p,save,*folders);},[save,folders](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not list backups. Finish PC transfer first. No save was changed.");return;}
        if(folders->empty()){App::Push<OptionBox>("No complete Nexus version 2 backup matches this game and user.\n\nUpload the whole backup folder using PC transfer > Upload save backups, then disconnect. Older Nexus backups and DBI formats are not accepted for restore.","Close",[](auto){});return;}
        auto choices=std::make_unique<Sidebar>("Restore backup",save.name,Sidebar::Side::RIGHT);
        for(const auto& folder:*folders)choices->Add<SidebarEntryCallback>(folder.substr(folder.find_last_of('/')+1),[save,folder]{
            App::Push<OptionBox>(nexusmanager::SaveDetails(save)+"\n\nReplace this save with:\n"+folder+"\n\nClose the game first. All backup files will be checked. The current save will be backed up before any replacement. Keep power and SD connected until it finishes.","Cancel","Restore",0,[save,folder](auto answer){
                if(!answer || *answer!=1)return;auto detail=std::make_shared<std::string>();
                App::Push<ProgressBox>(0,"Restore save",save.name,[save,folder,detail](ProgressBox* p){const auto rc=nexusmanager::RestoreSave(p,save,folder,*detail);nexus::Log("manager-save-restore",rc);return rc;},[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close",[](auto){});});
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
    SetAction(Button::LEFT,Action{[this]{selected=selected>=view::Rows?selected-view::Rows:0;}});
    SetAction(Button::RIGHT,Action{[this]{if(Count())selected=std::min(selected+view::Rows,Count()-1);}});
}
void Menu::SetupActions(){
    actions_dirty=false;
    for(auto button:{Button::X,Button::Y,Button::SELECT})RemoveAction(button);
    SetAction(Button::A,Action{"Open",[this]{Open();}});
    SetAction(Button::B,Action{mode==Mode::Files?"Up":"Back",[this]{Back();}});
    SetAction(Button::START,Action{"Exit",App::Exit});
    if(mode==Mode::Saves){SetAction(Button::X,Action{"Refresh",[this]{Refresh();}});return;}
    SetAction(Button::X,Action{"Actions",[this]{FileActions();}});
    SetAction(Button::SELECT,Action{"Storage",[this]{Storage();}});
    if(mode==Mode::Files && clipboard && nexus::files::SafeDirectory(path))SetAction(Button::Y,Action{"Paste",[this]{Paste();}});
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
        App::Push<ProgressBox>(0,"Finding game saves","Read-only inventory",[rows](ProgressBox* p){return nexusmanager::ListSaves(p,*rows);},[this,rows](Result rc){
            saves.clear();if(R_FAILED(rc)){App::PushErrorBox(rc,"Could not list game saves. No saves were changed.");return;}saves=std::move(*rows);
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }else if(mode==Mode::Files){
        const auto source=usb;const auto folder=path;
        auto rows=std::make_shared<std::vector<nexusapp::Item>>();auto gen=std::make_shared<u64>(generation);
        App::Push<ProgressBox>(0,"Reading folder",(source?"USB:":"SD:")+folder,[rows,gen,source,folder](ProgressBox* p)->Result{
            return nexusfiles::ListFiles(p,source,folder,*rows,*gen);
        },[this,rows,gen,keep](Result rc){
            actions_dirty=true;
            files.clear();if(R_FAILED(rc)){generation=0;path="/";mode=Mode::Volumes;App::PushErrorBox(rc,"Could not read this storage. Reconnect it, then choose it again. No files were changed.");return;}
            files=std::move(*rows);generation=*gen;
            const auto previous=std::find_if(files.begin(),files.end(),[&](const auto& item){return item.path==keep;});
            selected=previous==files.end()?0:size_t(previous-files.begin());
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }else GetPolledData(true);
}
void Menu::Inspect(const nexusapp::Item& item,bool hash){
    const auto source=usb;const auto gen=generation;auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,hash?"File checksum":"Read-only file preview",nexusmanager::Display(item.name),[item,source,gen,hash,detail](ProgressBox* p){
        return nexusmanager::InspectFile(p,source,gen,item,hash,*detail);
    },[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close",[](auto){});},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::Storage(){
    if(mode==Mode::Saves || !FullMemory())return;
    auto choices=std::make_unique<Sidebar>("Choose storage",clipboard?"Your clipboard selection is kept":"File manager",Sidebar::Side::RIGHT);
    for(bool target:{false,true})choices->Add<SidebarEntryCallback>(target?"USB drive":"SD card",[this,target]{
        usb=target;generation=0;path="/";files.clear();selected=0;focus_path.clear();mode=Mode::Files;Refresh();
    });
    choices->Add<SidebarEntryCallback>("Network locations",[]{devoptab::DisplayNetworkLocations();},"Browse SMB/NFS shares and copy verified files to USB.");
    if(clipboard)choices->Add<SidebarEntryCallback>("Clear clipboard",[this]{ClearClipboard();});
    App::Push(std::move(choices));
}
void Menu::ClearClipboard(){clipboard.reset();cut=false;actions_dirty=true;App::Notify("Clipboard cleared. No files were changed.");}
void Menu::SelectClipboard(const nexusapp::Item& item,bool move){
    clipboard=nexusfiles::Selection{usb,generation,item};cut=move;actions_dirty=true;
    App::Notify(std::string(move?"Ready to move: ":"Ready to copy: ")+nexusmanager::Display(item.name)+". Choose a destination folder, then Paste.");
}
void Menu::FileActions(){
    if(mode==Mode::Saves || !FullMemory())return;
    const bool item_selected=mode==Mode::Files && selected<files.size();
    const auto item=item_selected?files[selected]:nexusapp::Item{};
    const bool editable=item_selected && nexus::files::Safe(item.path);
    const bool retained_stage=item_selected && nexus::files::StagePath(item.path);
    const auto subtitle=!item_selected?"Storage and clipboard":retained_stage?"Temporary transfer item":!editable?"Managed data / read only":item.directory?"Folder actions":"File actions";
    auto choices=std::make_unique<Sidebar>(item_selected?nexusmanager::Display(item.name):"File manager",subtitle,Sidebar::Side::RIGHT);
    if(item_selected){
        if(editable){
            choices->Add<SidebarEntryCallback>("Copy",[this,item]{SelectClipboard(item,false);});
            choices->Add<SidebarEntryCallback>("Cut",[this,item]{SelectClipboard(item,true);});
        }
        if(editable || retained_stage)choices->Add<SidebarEntryCallback>("Delete",[this,item]{Remove(item);});
        const auto source=usb;
        choices->Add<SidebarEntryCallback>("Details",[item,source,editable,retained_stage]{
            const auto detail=std::string(source?"USB:":"SD:")+nexusmanager::Display(item.path)+"\n\n"+
                (item.directory?std::string("Folder."):"Size: "+utils::formatSizeStorage(item.size))+"\n\n"+
                (retained_stage?"Incomplete transfer item. Delete reviews its exact contents before removal. It cannot be copied or cut.":
                    editable?"Files and subfolders are counted before copying, moving or deleting.":"Managed game or system data is read only here. Use My games to manage installed games.");
            App::Push<OptionBox>(detail,"Close",[](auto){});
        });
        if(!item.directory){
            choices->Add<SidebarEntryCallback>("Preview first 256 bytes",[this,item]{Inspect(item,false);});
            choices->Add<SidebarEntryCallback>("Calculate SHA-256",[this,item]{App::Push<OptionBox>("Read the whole file to calculate its SHA-256 checksum? Large files can take time. No file is changed.","Cancel","Calculate",0,[this,item](auto answer){if(answer && *answer==1)Inspect(item,true);});});
        }
    }
    if(clipboard && mode==Mode::Files && nexus::files::SafeDirectory(path))choices->Add<SidebarEntryCallback>("Paste here",[this]{Paste();});
    if(clipboard)choices->Add<SidebarEntryCallback>("Clear clipboard",[this]{ClearClipboard();});
    choices->Add<SidebarEntryCallback>(mode==Mode::Files?"Refresh folder":"Refresh storage",[this]{Refresh();});
    App::Push(std::move(choices));
}
void Menu::Paste(){
    if(mode!=Mode::Files || !clipboard || !FullMemory())return;
    if(!nexus::files::SafeDirectory(path)){App::Push<OptionBox>("This folder contains managed game or system data and is read only here. Choose another destination, or use My games for installed games.","Close",[](auto){});return;}
    const auto source=*clipboard;const auto destination_usb=usb;const auto destination_generation=generation;
    const auto folder=path;const bool move=cut;
    auto plan=std::make_shared<nexusfiles::Plan>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,move?"Preparing move":"Preparing copy",nexusmanager::Display(source.item.name),[source,destination_usb,destination_generation,folder,move,plan,detail](ProgressBox* p){
        return nexusfiles::PrepareTransfer(p,source,destination_usb,destination_generation,folder,move,*plan,*detail);
    },[this,plan,detail,move](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"Could not prepare this transfer. No files were changed.":*detail);return;}
        const auto review=nexusfiles::TransferPreview(*plan)+"\n\nExisting destination files are never overwritten. Keep storage connected.";
        App::Push<OptionBox>(review,"Cancel",move?"Move here":"Copy here",0,[this,plan,move](auto answer){
            if(!answer || *answer!=1)return;
            auto report=std::make_shared<nexusfiles::Report>();
            App::Push<ProgressBox>(0,move?"Moving files":"Copying files",nexusmanager::Display(plan->source.item.name),[plan,report](ProgressBox* p){
                return nexusfiles::Transfer(p,*plan,*report);
            },[this,plan,report,move](Result result){
                if(view::ClearCutAfterTransfer(move,R_SUCCEEDED(result),report->complete,report->removed,report->removal_started)){clipboard.reset();cut=false;}
                actions_dirty=true;pending=true;
                if(report->published)focus_path=plan->final_path;
                const auto message=report->detail.empty()?(R_SUCCEEDED(result)?"Transfer complete.":"Transfer stopped. Check the storage connection before trying again."):report->detail;
                if(R_FAILED(result))App::PushErrorBox(result,message);else App::Push<OptionBox>(message,"Done",[](auto){});
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::Remove(const nexusapp::Item& item){
    if(mode!=Mode::Files || !FullMemory())return;
    const nexusfiles::Selection source{usb,generation,item};
    auto plan=std::make_shared<nexusfiles::DeletePlan>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Reviewing deletion",nexusmanager::Display(item.name),[source,plan,detail](ProgressBox* p){
        return nexusfiles::PrepareDelete(p,source,*plan,*detail);
    },[this,plan,detail](Result rc){
        if(R_FAILED(rc)){App::PushErrorBox(rc,detail->empty()?"Could not prepare deletion. No files were removed.":*detail);return;}
        App::Push<OptionBox>(nexusfiles::DeletePreview(*plan)+"\n\nThis permanently deletes the selected item and its contents. It cannot be undone.","Cancel","Delete",0,[this,plan](auto answer){
            if(!answer || *answer!=1)return;
            auto report=std::make_shared<nexusfiles::Report>();
            App::Push<ProgressBox>(0,"Deleting files",nexusmanager::Display(plan->source.item.name),[plan,report](ProgressBox* p){
                return nexusfiles::Delete(p,*plan,*report);
            },[this,plan,report](Result result){
                if(clipboard && (report->removal_started || report->removed || report->complete) && clipboard->usb==plan->source.usb &&
                   clipboard->generation==plan->source.generation && view::IsSameOrChild(clipboard->item.path,plan->source.item.path)){
                    clipboard.reset();cut=false;
                }
                actions_dirty=true;pending=true;
                const auto message=report->detail.empty()?(R_SUCCEEDED(result)?"Deletion complete.":"Deletion stopped. Some items may remain. Refresh the folder before continuing."):report->detail;
                if(R_FAILED(result))App::PushErrorBox(result,message);else App::Push<OptionBox>(message,"Done",[](auto){});
            },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
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
    auto choices=std::make_unique<Sidebar>(save.name,"Save backup and restore",Sidebar::Side::RIGHT);
    choices->Add<SidebarEntryCallback>("Back up save",[save]{App::Push<OptionBox>(nexusmanager::SaveDetails(save)+"\n\nBack up this save to SD? The original is kept. Close the game first. Every copied file is verified.","Cancel","Back up",0,[save](auto answer){
        if(!answer || *answer!=1)return;
        auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Save backup",save.name,[save,detail](ProgressBox* p){
            const auto rc=nexusmanager::BackupSave(p,save,*detail);nexus::Log("manager-save-backup",rc);return rc;
        },[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close",[](auto){});},ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
    });},true);
    choices->Add<SidebarEntryCallback>("Restore a backup",[save]{ChooseRestore(save);},true);
    App::Push(std::move(choices));
}
void Menu::Update(Controller* c,TouchInfo* touch){
    if(actions_dirty)SetupActions();
    MenuBase::Update(c,touch);
    if(pending && HasFocus()){pending=false;if(!App::IsApplet())Refresh();}
    if(HasFocus() && touch->is_clicked){
        const auto begin=selected/view::Rows*view::Rows;
        for(size_t i=begin;i<std::min(Count(),begin+view::Rows);++i){const auto row=view::Row(i);if(touch->in_range({row.x,row.y,row.w,row.h})){
            if(selected==i)Open();else selected=i;break;
        }}
    }
    SetSubHeading(std::to_string(Count()?selected+1:0)+" / "+std::to_string(Count()));
}
void Menu::Draw(NVGcontext* vg,Theme* theme){
    MenuBase::Draw(vg,theme);violet::Card(vg,{42,106,1196,533},false);
    const auto fg=theme->GetColour(ThemeEntryID_TEXT),muted=violet::Lilac(),accent=violet::Accent();
    const std::string heading=mode==Mode::Volumes?"File manager":mode==Mode::Saves?"Save backups":(usb?"File manager / USB":"File manager / SD card");
    Text(vg,62,114,27,fg,heading);
    nvgSave(vg);nvgScissor(vg,62,147,1150,23);
    Text(vg,62,147,15,muted,mode==Mode::Files?std::string(usb?"USB:":"SD:")+nexusmanager::Display(path)+(nexus::files::SafeDirectory(path)?"":"  /  Managed data: read only"):mode==Mode::Saves?"Choose a game and user / back up or restore with confirmation":"Copy or cut an item, choose its destination, then Paste. Folders include all their contents.");nvgRestore(vg);
    const auto begin=selected/view::Rows*view::Rows;
    for(size_t i=begin;i<std::min(Count(),begin+view::Rows);++i){
        const auto row=view::Row(i);const float y=row.y;violet::Card(vg,{row.x,row.y,row.w,row.h},i==selected,true);
        std::string label,right;
        if(mode==Mode::Volumes){label=i==2?"Network locations":i?"USB drive":"SD card";right=i==2?"SMB / NFS":"Open";}
        else if(mode==Mode::Saves){label=saves[i].name;right=saves[i].info.save_data_type==FsSaveDataType_Account?"User save":"Device save";}
        else {label=nexusmanager::Display(files[i].name);right=files[i].directory?"Folder":utils::formatSizeStorage(files[i].size);}
        if(mode==Mode::Files && files[i].directory){
            gfx::drawRect(vg,72,y+19,29,21,nvgRGBA(245,64,231,100),3);
            gfx::drawRect(vg,72,y+14,14,9,accent,2);
        }
        nvgSave(vg);nvgScissor(vg,114,y+4,820,50);Text(vg,114,y+17,20,fg,label);nvgRestore(vg);
        Text(vg,1208,y+19,16,accent,right,NVG_ALIGN_RIGHT|NVG_ALIGN_TOP);
    }
    if(!Count())Text(vg,72,205,20,muted,App::IsApplet()?"Use full-memory mode to access these tools.":mode==Mode::Saves?"No supported primary user/device saves found.":!nexus::files::SafeDirectory(path)?"Managed game or system data is read only here.":clipboard?"This folder is empty. Paste adds your selected item here.":"This folder is empty. Use Storage to choose another drive.");
    const auto footer=view::ClipboardLine();nvgSave(vg);nvgScissor(vg,footer.x,footer.y,footer.w,footer.h);
    const auto hint=mode==Mode::Saves?std::string("Backups stay on SD. Keep a separate copy on your PC."):
        clipboard?std::string(cut?"Ready to move: ":"Ready to copy: ")+(clipboard->usb?"USB:":"SD:")+nexusmanager::Display(clipboard->item.path):
        "X: item actions   /   Storage: change drive   /   Left or right: page";
    Text(vg,footer.x,footer.y+3,17,muted,hint);nvgRestore(vg);
}
}
