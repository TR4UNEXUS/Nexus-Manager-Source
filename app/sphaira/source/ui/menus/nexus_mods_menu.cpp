// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/nexus_menu.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "utils/utils.hpp"
namespace sphaira::ui::menu::nexusmenu {
void Menu::BrowseMods(){page=Page::ModFolders;usb_source=true;generation=0;path="/";files.clear();Refresh();}
void Menu::ModActions(){
    if(selected>=mods.size())return;
    const auto c=mods[selected];auto options=std::make_unique<Sidebar>(c.name,c.registered?"Registered / refresh safely":"Not registered",Sidebar::Side::RIGHT);
    if(c.romfs)options->Add<SidebarEntryCallback>("Review USB game data (RomFS)",[this,c]{ReviewMod(c,false);});
    if(c.exefs)options->Add<SidebarEntryCallback>(c.romfs?"Review game data + ExeFS":"Review ExeFS cache",[this,c]{ReviewMod(c,true);});
    if(c.ips)options->Add<SidebarEntryCallback>("Review this IPS group",[this,c]{ReviewMod(c,false);});
    options->Add<SidebarEntryCallback>("What gets registered?",[c]{
        App::Push<OptionBox>("USB Mods\n\n"+c.path+"\n\nRomFS stays on USB; a small index is written to SD. Selected ExeFS/IPS files are cached on SD. IPS is matched by its original build-ID filename, not assigned to a game by folder name.\n\nBoot files, system modules, cheats, KIPs and configuration files are excluded. Requires Nexus USB Mods M2 firmware.","Back","OK",0,[](auto){});
    });App::Push(std::move(options));
}
void Menu::ReviewMod(const nexus::mods::Candidate& c,bool exefs){
    const auto gen=generation;auto plan=std::make_shared<nexus::mods::Plan>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Review USB mod",c.name,[c,exefs,gen,plan,detail](ProgressBox* p){return nexus::mods::Review(p,c,exefs,gen,*plan,*detail);},
    [this,c,gen,plan,detail](Result rc){
        if(R_FAILED(rc)){Report(rc,*detail);return;}
        if(plan->changes.empty()){App::Notify("This registration is already up to date. No files changed.");return;}
        uint64_t bytes=0;for(auto& f:plan->changes)bytes+=f.bytes.size();
        const auto text="Register / refresh USB mod?\n\n"+c.name+"\n"+
            std::to_string(plan->file_count)+" RomFS files stay on USB ("+utils::formatSizeStorage(plan->data_bytes)+").\n"+
            std::to_string(plan->changes.size())+" SD cache/journal changes: "+utils::formatSizeStorage(bytes)+".\n\n"+
            (c.ips?"IPS is global build-ID matching, not tied to a title. Only choose trusted GAME patches for your exact version, never system patches. Filenames are preserved.\n\n":"")+
            "Close games and other installers first. Existing managed files are backed up; unrelated files are not replaced. Missing files are not pruned. Reboot after applying; keep USB connected.";
        App::Push<OptionBox>(text,"Cancel","Apply",0,[this,gen,plan,detail](auto choice){
            if(!choice||*choice!=1)return;
            App::Push<ProgressBox>(0,"Register USB mod","Verify, back up and activate",[plan,detail,gen](ProgressBox* p){return nexus::mods::Install(p,*plan,gen,*detail);},
            [this,detail](Result r){
                if(R_FAILED(r)){Report(r,*detail+"\n\nIf an update was interrupted, choose L: Recover in USB Mods before trying again. USB game data was not removed.");return;}
                App::Push<OptionBox>("USB mod registered.\n\nReboot before starting the game, with USB connected. Changes are discovered when the title starts; active games are not hot-reloaded.\n\nBackups: SD:/switch/usbloader/mod-backups\nOwnership: SD:/switch/usbloader/mod-cache\n\nAdd/change files later, then scan and refresh this title again.","Back","OK",0,[](auto){});refresh_pending=true;
            });
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::RecoverMods(){
    App::Push<OptionBox>("Recover an interrupted USB Mods update?\n\nOnly the pending, hash-verified SD changes made by this importer are rolled back. Modified/conflicting files are refused. USB game data and identity markers are kept.\n\nClose games/installers first. Completed registrations are not removed.","Cancel","Recover",0,[this](auto choice){
        if(!choice||*choice!=1)return;auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Recover USB mod update","Restoring verified SD cache files",[detail](ProgressBox* p){return nexus::mods::Restore(p,*detail);},[this,detail](Result rc){Report(rc,R_SUCCEEDED(rc)?"No pending update remains. USB data is untouched. Reboot if a registration was restored.":*detail);});
    });
}
}
