// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/nexus_menu.hpp"
#include "ui/menus/manager_tools_menu.hpp"
#include "ui/menus/extended_tools_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/nexus_layout.hpp"
#include "ui/nexus_library.hpp"
#include "ui/nexus_dialog.hpp"
#include "ui/nexus_icons.hpp"
#include "ui/nexus_violet.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "title_info.hpp"
#include "image.hpp"
#include "nexus/update.hpp"
#include "nexus/update_restart.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/vsd_policy.hpp"
#include "nexus/homebrew.hpp"
#include "nexus/port_transfer.hpp"
#include "nexus/port_catalog.hpp"
#include "swkbd.hpp"
#include "nexus/home_badges.hpp"
#include "nexus/completion.hpp"
#include "nexus/dump.hpp"
#include "nexus/index_recovery.hpp"
#include "nexus/recovery_ui.hpp"
#include "nexus/usb_health.hpp"
#include "nexus/usb_import.hpp"
#include "nexus/usb_library_startup.hpp"
#include "utils/devoptab.hpp"
#include "nexus/badge_pixels.hpp"
#include "nexus/watchdog.h"
#include "nro.hpp"
#include "utils/utils.hpp"
#include "utils/thread.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace sphaira::ui::menu::nexusmenu {
using nexus::Target;
namespace view=nexusview;
class InstallSourcesSidebar final:public Sidebar {
    const std::function<void(Sidebar&)> populate;
    bool focused_once{};
public:
    explicit InstallSourcesSidebar(std::function<void(Sidebar&)> fill)
        :Sidebar("Install games","Choose where your install files are stored",Sidebar::Side::RIGHT,1280.f),populate(std::move(fill)){populate(*this);}
    void OnFocusGained() noexcept override {
        // Settings can be saved while this list remains beneath a child menu.
        // Rebuild on return, after its old row callbacks have finished.
        if(focused_once){ClearEntries();populate(*this);}
        focused_once=true;Sidebar::OnFocusGained();
    }
};
struct IconPainter {
    NVGcontext* vg;
    void Path(std::initializer_list<view::Point> points,bool close) {
        nvgBeginPath(vg);bool first=true;
        for(auto p:points){if(first)nvgMoveTo(vg,p.x,p.y);else nvgLineTo(vg,p.x,p.y);first=false;}
        if(close)nvgClosePath(vg);
        nvgStroke(vg);
    }
    void Rect(float x,float y,float w,float h,float radius) {
        nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,radius);nvgStroke(vg);
    }
    void Circle(float x,float y,float radius) {
        nvgBeginPath(vg);nvgCircle(vg,x,y,radius);nvgStroke(vg);
    }
};
static void MenuIcon(NVGcontext* vg,view::MenuIcon icon,float x,float y,float size,NVGcolor colour) {
    nvgSave(vg);nvgTranslate(vg,x,y);nvgScale(vg,size/96,size/96);
    nvgStrokeColor(vg,colour);nvgStrokeWidth(vg,3.2f);nvgLineCap(vg,NVG_ROUND);nvgLineJoin(vg,NVG_ROUND);
    IconPainter painter{vg};view::PaintIcon(painter,icon);nvgRestore(vg);
}
static std::string StorageLabel(const nexusapp::Game& game) {
    if(!game.homebrew_path.empty())return "USB app";
    if(!game.location_checked)return game.location_hint.empty()?"Storage not checked":game.location_hint;
    std::string label;
    const char* names[]={"SD","Internal","USB"};
    for(size_t i=0;i<3;++i)if(game.bytes[i]){if(!label.empty())label+=" + ";label+=names[i];}
    if(!game.forwarder_path.empty())return game.usb_forwarder?"USB port shortcut":"SD port shortcut";
    return label.empty()?"Unavailable":label;
}
static std::string PrimaryLabel(const nexusapp::Game& game) {
    if(!game.homebrew_path.empty())return "App options";
    if(!game.forwarder_path.empty())return game.usb_forwarder?"Move port to SD":"Move port to USB";
    const auto route=view::PrimaryRoute(game.bytes,game.location_checked);
    return route.available?std::string("Move to ")+nexus::TargetName(route.destination):"Move game";
}
static void Text(NVGcontext* vg,float x,float y,float size,int align,const NVGcolor& colour,const char* str){
    gfx::drawText(vg,x,y,size,colour,str,align);
}
template<class Measure>
static std::string FitLineText(std::string_view text,float width,Measure measure){
    if(width<=0||text.empty())return {};
    if(measure(text)<=width)return std::string(text);
    constexpr std::string_view suffix="...";
    if(measure(suffix)>width)return {};
    // Search complete UTF-8 character boundaries, never cut inside a glyph.
    std::vector<size_t> ends{0};
    for(size_t i=1;i<=text.size();++i)if(i==text.size()||(static_cast<unsigned char>(text[i])&0xc0)!=0x80)ends.push_back(i);
    size_t first=0,last=ends.size()-1;
    while(first<last){const auto middle=(first+last+1)/2;const auto value=std::string(text.substr(0,ends[middle]))+std::string(suffix);
        if(measure(value)<=width)first=middle;else last=middle-1;}
    return std::string(text.substr(0,ends[first]))+std::string(suffix);
}
static void BoundedLine(NVGcontext* vg,const Vec4& r,float size,const NVGcolor& colour,const std::string& text,bool right=false){
    if(r.w<=0||r.h<=0)return;
    nvgSave(vg);nvgFontSize(vg,size);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);
    const auto fitted=FitLineText(text,r.w,[&](std::string_view value){return nvgTextBounds(vg,0,0,value.data(),value.data()+value.size(),nullptr);});
    nvgIntersectScissor(vg,r.x,r.y,r.w,r.h);
    Text(vg,right?r.x+r.w:r.x,r.y+r.h/2,size,(right?NVG_ALIGN_RIGHT:NVG_ALIGN_LEFT)|NVG_ALIGN_MIDDLE,colour,fitted.c_str());
    nvgRestore(vg);
}
Menu::Menu(u32 flags):MenuBase("Nexus Manager",flags) {
    SetTitleSubHeading(std::string("Nexus Manager ")+nexus::updates::Version);
    SetAction(Button::A,Action{"Open",[this]{Activate();}});
    SetAction(Button::B,Action{"Back",[this]{Back();}});
    SetAction(Button::X,Action{"Refresh",[this]{Refresh();}});
    SetAction(Button::SELECT,Action{"Options",[this]{Updates();}});
    SetAction(Button::LEFT,Action{[this]{if(page==Page::Installed)selected=selected>=view::PageSize?selected-view::PageSize:0;else if(selected)--selected;}});
    SetAction(Button::RIGHT,Action{[this]{if(Count())selected=std::min(selected+(page==Page::Installed?view::PageSize:1),Count()-1);}});
    SetAction(Button::UP,Action{[this]{if(page==Page::Installed)selected=view::Vertical(selected,Count(),false);else if(page==Page::Home)selected=view::HomeVertical(selected,false);else if(page==Page::More)selected=view::MoreVertical(selected,false);else if(selected)--selected;}});
    SetAction(Button::DOWN,Action{[this]{if(page==Page::Installed)selected=view::Vertical(selected,Count(),true);else if(page==Page::Home)selected=view::HomeVertical(selected,true);else if(page==Page::More)selected=view::MoreVertical(selected,true);else if(selected+1<Count())++selected;}});
}
Menu::~Menu(){
    nexusapp::StopUsbLibraryStartupCheck();
    nexus::updates::StopStartupCheck();
    // Shutdown watchdog may close the whole process if this read never returns;
    // never abandon a worker that still owns this job/buffer.
    if(icon_thread_started){threadWaitForExit(&icon_thread);threadClose(&icon_thread);}
    for(const auto& [id,image]:icons)nvgDeleteImage(App::GetVg(),image);
    if(title_service_ready)title::Exit();
}
void Menu::ReadIconWorker(void* arg){
    auto& job=*static_cast<IconJob*>(arg);
    job.result=nexusapp::ReadHomebrewIcon(job.game,job.bytes);
    job.done.store(true);
}
void Menu::QueueIcon(const nexusapp::Game& game){
    if(icon_job || icon_checked.contains(game.CacheKey()))return;
    icon_checked.insert(game.CacheKey());
    icon_job=std::make_unique<IconJob>();icon_job->game=game;icon_job->started=armGetSystemTick();
    const auto rc=utils::CreateThread(&icon_thread,ReadIconWorker,icon_job.get(),64*1024);
    if(R_FAILED(rc)){icon_job.reset();return;}
    if(R_FAILED(threadStart(&icon_thread))){threadClose(&icon_thread);icon_job.reset();return;}
    icon_thread_started=true;
}
void Menu::PollIcon(){
    if(!icon_job)return;
    if(!icon_job->done.load()){
        if(!icon_job->warned && armGetSystemTick()-icon_job->started>=armNsToTicks(15ULL*1000*1000*1000)){
            icon_job->warned=true;App::Notify("USB icon read is busy. You can still use Back; keep the drive connected.");
        }
        return;
    }
    threadWaitForExit(&icon_thread);threadClose(&icon_thread);icon_thread_started=false;
    const auto key=icon_job->game.CacheKey();
    // A refreshed library or changed page must not receive a stale icon.
    if(page==Page::Installed && icon_checked.contains(key) && !icons.contains(key) && R_SUCCEEDED(icon_job->result)){
        auto img=ImageLoadIcon(icon_job->bytes);
        if(!img.data.empty()){
            if(img.w!=256 || img.h!=256)img=ImageResize(img.data,img.w,img.h,256,256);
            nexus_badge::Overlay(img.data.data(),img.data.size(),256);
            const auto handle=nvgCreateImageRGBA(App::GetVg(),img.w,img.h,0,img.data.data());
            if(handle>0)icons[key]=handle;
        }
    }
    icon_job.reset();
}
size_t Menu::Count() const {
    if(page==Page::Mods)return mods.size();
    if(page==Page::ModFolders)return files.size();
    if(page==Page::Queue)return packages.Entries().size();
    if(page==Page::Results)return batch_result?batch_result->items.size():0;
    return page==Page::Home?view::HomeCount:page==Page::More?view::MoreCount:page==Page::Installed?games.size():(page==Page::Files || page==Page::Homebrew)?files.size():cleanup_items.size();
}
void Menu::Report(Result rc,const std::string& text) {
    if(R_FAILED(rc)) App::PushErrorBox(rc,text);
    else App::Notify(text);
}
void Menu::ChooseSource(bool usb){packages.Clear();usb_source=usb;generation=0;path="/";selected=0;page=Page::Files;Refresh();}
void Menu::Back(){
    actions_dirty=true;
    if(page==Page::ModFolders){
        if(path!="/"){auto pos=path.find_last_of('/');path=pos?path.substr(0,pos):"/";Refresh();}
        else {page=Page::Mods;Refresh();}return;
    }
    if(page==Page::Queue || page==Page::Results){page=Page::Files;selected=0;return;}
    if((page==Page::Files || page==Page::Homebrew) && path!="/") {
        const auto end=path.find_last_of('/');path=end?path.substr(0,end):"/";selected=0;Refresh();
    } else if(page==Page::Files && !packages.Empty()){
        App::Push<OptionBox>("Leave this source?\n\nThe selected-package queue will be cleared. No files will be deleted.","Stay","Clear queue + leave",0,[this](auto choice){if(choice && *choice==1){packages.Clear();page=Page::Home;selected=0;actions_dirty=true;}});
    } else if(page==Page::Homebrew || page==Page::Mods){selected=page==Page::Homebrew?2:4;page=Page::More;
    } else {page=Page::Home;selected=0;}
}
void Menu::Refresh(){
    actions_dirty=true;
    GetPolledData(true);
    const auto previous_index=selected;
    const auto previous_key=page==Page::Installed && selected<games.size()?games[selected].CacheKey():std::string{};
    if(page!=Page::Installed)selected=0;
    if(page==Page::Mods){
        auto result=std::make_shared<std::vector<nexus::mods::Candidate>>();auto detail=std::make_shared<std::string>();auto gen=std::make_shared<u64>();const auto folder=mod_folder;
        App::Push<ProgressBox>(0,"Find USB mods",folder,[result,detail,gen,folder](ProgressBox* p){return nexus::mods::Find(p,folder,*result,*gen,*detail);},[this,result,detail,gen](Result rc){
            mods.clear();if(R_FAILED(rc)){Report(rc,*detail);return;}mods=std::move(*result);generation=*gen;
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    } else if(page==Page::Installed){
        auto result=std::make_shared<std::vector<nexusapp::Game>>();
        App::Push<ProgressBox>(0,"Loading your games","Reading game locations; no full-file verification",[this,result](ProgressBox* p){
            if(!title_service_ready){R_TRY(title::Init());title_service_ready=true;}
            R_TRY(p->ShouldExitResult());
            R_TRY(nexusapp::ScanGames(p,*result));
            return nexusapp::ScanGameLocations(p,*result);
        },[this,result,previous_key,previous_index](Result rc){
            if(R_FAILED(rc)){Report(rc,"Installed scan failed; no content was changed.");return;}
            title::Clear(false);title_requested.clear();
            games=std::move(*result);
            const auto previous=std::find_if(games.begin(),games.end(),[&](const auto& game){return game.CacheKey()==previous_key;});
            selected=previous!=games.end()?size_t(previous-games.begin()):games.empty()?0:std::min(previous_index,games.size()-1);
            actions_dirty=true;
            for(const auto& [key,image]:icons)nvgDeleteImage(App::GetVg(),image);
            icons.clear();icon_checked.clear();
            // The visible page queues names/icons in Update. Do not queue the
            // entire library ahead of the game the user is trying to select.
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    } else if(page==Page::Files || page==Page::Homebrew || page==Page::ModFolders){
        auto result=std::make_shared<std::vector<nexusapp::Item>>();
        auto gen=std::make_shared<u64>(generation);
        const auto usb=usb_source;const auto folder=path;const bool homebrew=page==Page::Homebrew;
        App::Push<ProgressBox>(0,"Browse",usb?"USB (shared drive)":"SD card",[result,gen,usb,folder,homebrew](ProgressBox* p){
            R_TRY(homebrew?nexusapp::ListHomebrew(folder,*result,*gen,usb):nexusapp::ListFiles(usb,folder,*result,*gen));
            return p->ShouldExitResult();
        },[this,result,gen,homebrew,folder,usb](Result rc){
            files.clear();if(R_FAILED(rc)){packages.Clear();Report(rc,"Could not read this drive. Selections were cleared. Reconnect, then reopen the source from Nexus Manager.");return;}
            generation=*gen;files=std::move(*result);
            if(page==Page::ModFolders)std::erase_if(files,[](const auto& e){return !e.directory;});
#if NEXUS_BUNDLED_PROBE
            if(homebrew && usb && folder=="/")files.insert(files.begin(),{"Run USB diagnostics","",0,false});
#endif
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    } else if(page==Page::Cleanup){if(cleanup_tickets)ScanTickets();else ScanCleanupTarget(cleanup_target);}
}
void Menu::ChooseInstall(const nexusapp::Item& item){
    const nexus::batch::Entry entry{item.name,item.path,item.size};
    if(item.directory || !nexus::batch::ValidEntry(entry)){
        Report(nexus::Invalid,"Select an NSP, XCI, NSZ or XCZ package.");return;
    }
    auto sidebar=std::make_unique<Sidebar>("Where to install?","The original file is kept",Sidebar::Side::RIGHT);
    for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
        sidebar->Add<SidebarEntryCallback>(nexus::TargetName(target),[this,entry,target]{StartBatch(target,{entry});});
    }
    App::Push(std::move(sidebar));
}
void Menu::LaunchHomebrew(const nexusapp::Item& item,u64 gen){
    // Native applications have no hbloader next-NRO ABI. Stop BEFORE arming
    // the virtual-SD session; the existing explicit HOME shortcut is supported.
    if(envIsNso()){
        App::Push<OptionBox>("Standalone edition\n\nTo run USB homebrew, choose Create HOME shortcut, then open that shortcut from HOME.\n\nDirect launch is available in the NRO edition. No USB launch session was opened.","Back",[](auto){});return;
    }
    nexus_vsd::Profile profile{};
    if(!gen || item.directory || !nexus_vsd::Parse(item.path,profile)){
        App::Push<OptionBox>("NRO location not supported\n\nUse USB:/switch/<port>/<port>.nro for a normal USB-root layout, or USB:/homebrew/<profile>/sdmc/... for an isolated profile.\n\nKeep the port's data files with it. Paths must be under 256 bytes with no .. segments. No files were changed.","Back","OK",0,[](auto){});return;
    }
    auto result=std::make_shared<NexusVsdStatus>();
    App::Push<ProgressBox>(0,"Checking USB homebrew",item.name,[result](ProgressBox* p){
        nexus::Client client;const auto rc=client.EnsureVirtualSdLoader(*result);
        nexus::Log("homebrew-loader-status",rc,result->flags);
        R_TRY(rc);return p->ShouldExitResult();
    },[this,item,gen,profile,result](Result check){
    const auto& status=*result;
    if(R_FAILED(check) || status.flags!=NEXUS_VSD_LOADER_SEEN){
        Report(R_FAILED(check)?check:nexus::Unavailable,"USB homebrew needs the matching Virtual SD companion and homebrew loader.\n\nUse this release's Nexus Manager HOME-menu NSP, or full-memory R-title override with the matching atmosphere/hbl.nsp. Old forwarders may embed a different loader. Normal installs remain available.");return;
    }
    if(nexus_vsd::UsbRoot(profile) && !(status.reserved&NEXUS_VSD_CAP_USB_ROOT)){
        Report(nexus::Unavailable,"This companion only supports isolated profiles.\n\nInstall the matching 0.5.1-preview firmware package and reboot to launch your normal USB:/switch layout. The app update alone cannot enable that route.");return;
    }
    const auto warning=nexus_vsd::UsbRoot(profile)?"\n\nUSB-root mode: this homebrew can read and write the WHOLE USB drive, including game folders. Only launch trusted homebrew. The real microSD is not merged into this view.":"\n\nThis homebrew uses its isolated USB profile.";
    App::Push<OptionBox>("Launch USB homebrew?\n\n"+item.name+"\nsdmc:/ maps to USB:"+profile.root+warning+"\n\nExisting SD saves are NOT copied. Keep USB connected. Compatibility is experimental.","Cancel","Launch",0,[this,item,profile,gen](auto choice){
        if(!choice || *choice!=1)return;
        App::Push<ProgressBox>(0,"Preparing USB homebrew",item.name,[item,gen](ProgressBox* p){
            nexus::Client owner;
            R_TRY(p->ShouldExitResult());
            const auto rc=owner.ArmVirtualSd(gen,item.path);
            nexus::Log("homebrew-arm",rc,gen);
            R_TRY(rc);
            if(p->ShouldExit()) {
                R_TRY(owner.CancelVirtualSd());
                return p->ShouldExitResult();
            }
            return Result{0};
        },[this,profile](Result rc){
        if(R_FAILED(rc)){Report(rc,"USB homebrew was not launched. Close Nexus Manager before retrying; the drive or session may be busy.");return;}
        nexusWatchdogArm(30ULL*1000*1000*1000); // includes play-log/exit IPC before main loop returns
        rc=nro_launch(profile.logical); // UI-thread handoff only after preparation finished
        if(R_FAILED(rc)){
            nexusWatchdogDisarm();
            App::Push<ProgressBox>(0,"Cancelling USB homebrew","Closing unused launch session",[](ProgressBox*){
                nexus::Client owner;const auto cancel=owner.CancelVirtualSd();
                nexus::Log("homebrew-cancel",cancel);return cancel;
            },[this,rc](Result cancel){
                (void)cancel;
                Report(rc,"Could not hand off to the homebrew loader. Close Nexus Manager before retrying if cancellation failed.");
            });
        }
        });
    });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::HomebrewActions(const nexusapp::Item& item,u64 gen,bool usb){
    auto sidebar=std::make_unique<Sidebar>(item.name,usb?"Homebrew on USB":"Homebrew on SD - startup timing",Sidebar::Side::RIGHT,1280.f);
    if(!usb){
        sidebar->Add<SidebarEntryCallback>("Create SD timing shortcut",[this,item]{CreateSdTimingForwarder(item);});
        App::Push(std::move(sidebar));return;
    }
    if(!envIsNso())sidebar->Add<SidebarEntryCallback>("Open app",[this,item,gen]{LaunchHomebrew(item,gen);});
    sidebar->Add<SidebarEntryCallback>("Create HOME shortcut",[this,item,gen]{CreateForwarder(item,gen);});
    sidebar->Add<SidebarEntryCallback>("Create USB timing shortcut",[this,item,gen]{CreateForwarder(item,gen,true);});
    App::Push(std::move(sidebar));
}
void Menu::CreateSdTimingForwarder(const nexusapp::Item& item){
    if(R_FAILED(nexusapp::CheckStartupTimingSupport())){
        Report(nexus::Unavailable,"Startup timing needs SD8 firmware. Install SD8 and reboot first.");return;
    }
    App::Push<OptionBox>("Create a separate SD timing shortcut?\n\n"+item.name+"\n"+item.path+"\n\nLaunch it from HOME, follow the same steps as the USB test, and leave it running for at least 60 seconds. Close the game and export startup captures with X in the benchmark app before rebooting.\n\nBoth timing shortcuts use the same memory and CPU settings. The launcher is installed internally; app files stay on SD.","Cancel","Create",0,[this,item](auto choice){
        if(!choice || *choice!=1)return;
        App::Push<ProgressBox>(0,"Create SD timing shortcut",item.name,[item](ProgressBox* p){
            return nexusapp::CreateSdTimingForwarder(p,item);
        },[this](Result rc){Report(rc,R_SUCCEEDED(rc)?"SD timing shortcut installed. Exit Nexus Manager and launch it from HOME.":"Could not create SD timing shortcut. Existing titles and app files were not replaced. If this timing shortcut exists, remove it in System Settings before recreating it.");refresh_pending=true;});
    });
}
void Menu::CreateForwarder(const nexusapp::Item& item,u64 gen,bool timing){
    if(timing && R_FAILED(nexusapp::CheckStartupTimingSupport())){
        Report(nexus::Unavailable,"Startup timing needs SD8 firmware. Install SD8 and reboot first.");return;
    }
    const auto timing_help=timing?std::string("\n\nThis separate timing shortcut captures the first 60 seconds of filesystem activity. Follow the same steps as the SD test, then close the game and export startup captures with X in the benchmark app before rebooting."):std::string{};
    App::Push<OptionBox>("Create a HOME shortcut?\n\n"+item.name+"\n"+item.path+"\n\nA small launcher is installed to Internal Storage. The app, data and saves stay on USB.\n\nOnly use trusted homebrew: it can write to the USB drive. Keep USB connected. After replacing the app's NRO file, remove the shortcut and recreate it. Existing titles are never replaced."+timing_help,"Cancel","Create",0,[this,item,gen,timing](auto choice){
        if(!choice || *choice!=1)return;
        App::Push<ProgressBox>(0,"Create USB forwarder",item.name,[item,gen,timing](ProgressBox* p){
            const auto rc=nexusapp::CreateHomebrewForwarder(p,item,gen,timing);
            nexus::Log("homebrew-forwarder-create",rc);return rc;
        },[this](Result rc){
            Report(rc,R_SUCCEEDED(rc)?"HOME forwarder installed. Exit Nexus Manager, then launch it from HOME with USB connected.":
                "Forwarder creation stopped. Existing titles and USB files were not replaced. If this shortcut already exists, remove it in System Settings before recreating it. An interrupted install may leave an incomplete launcher/placeholder.");
            refresh_pending=true;
        });
    });
}
void Menu::RunProbe(){
    if(App::IsApplet()){Report(nexus::Unavailable,"Open Nexus Manager through full-memory R-title override first.");return;}
    auto result=std::make_shared<NexusVsdStatus>();
    App::Push<ProgressBox>(0,"Checking USB diagnostics","Checking the companion",[result](ProgressBox* p){
        nexus::Client client;R_TRY(client.EnsureVirtualSdLoader(*result));return p->ShouldExitResult();
    },[this,result](Result rc){
    const auto& status=*result;
    if(R_FAILED(rc) || status.flags!=NEXUS_VSD_LOADER_SEEN){Report(R_FAILED(rc)?rc:nexus::Unavailable,"Built-in USB diagnostics needs the matching Virtual SD package3 and hbl.nsp. Reboot after installing, then use full-memory R-title override.");return;}
    const auto gen=generation;
    App::Push<OptionBox>("Run built-in USB diagnostics?\n\nNexus Manager will copy its bundled probe to a NEW private folder on USB, verify it, then offer Launch. No separate download or manual copy is needed.\n\nIt tests USB-backed sdmc reads, writes, seek and directory operations. Existing files are not replaced. Keep USB connected.","Cancel","Prepare test",0,[this,gen](auto choice){
        if(!choice || *choice!=1)return;
        auto item=std::make_shared<nexusapp::Item>();
        App::Push<ProgressBox>(0,"USB diagnostics","Preparing bundled probe",[item,gen](ProgressBox* p){
            const auto rc=nexusapp::PrepareProbe(p,gen,*item);
            nexus::Log("homebrew-probe-prepare",rc);return rc;
        },[this,item](Result rc){
            if(R_FAILED(rc)){Report(rc,"Probe preparation stopped. No existing files were replaced; a new incomplete usbloader-probe folder may remain on USB.");return;}
            LaunchHomebrew(*item,generation);
        });
    });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::SetupActions(){
    actions_dirty=false;
    for(auto button:{Button::X,Button::Y,Button::START,Button::SELECT,Button::L,Button::R})RemoveAction(button);
    SetAction(Button::A,Action{page==Page::Queue || page==Page::Results?"Details":"Open",[this]{Activate();}});
    SetAction(Button::B,page==Page::Home?Action{[this]{Back();}}:Action{"Back",[this]{Back();}});
    if(page==Page::Installed){
        SetAction(Button::A,Action{"Manage",[this]{GameActions();}});
        SetAction(Button::Y,Action{"Refresh",[this]{Refresh();}});
        SetAction(Button::R,Action{[this]{OpenUsbRepair();}});
        SetAction(Button::SELECT,Action{[this]{Updates();}});
        SetAction(Button::START,Action{App::Exit});
    }else if(page==Page::Cleanup){
        SetAction(Button::A,Action{"Review item",[this]{Activate();}});
        SetAction(Button::X,Action{"Refresh",[this]{Refresh();}});
        if(!cleanup_items.empty())SetAction(Button::Y,Action{"Delete all",[this]{DeleteAllCleanup();}});
        SetAction(Button::START,Action{"Exit",App::Exit});
    }else if(page==Page::Mods){
        SetAction(Button::X,Action{"Refresh",[this]{Refresh();}});
        SetAction(Button::Y,Action{"Choose folder",[this]{BrowseMods();}});
        SetAction(Button::L,Action{"Recover",[this]{RecoverMods();}});
    }else if(page==Page::ModFolders){
        SetAction(Button::X,Action{"Use folder",[this]{mod_folder=path;page=Page::Mods;Refresh();}});
    }else if(page==Page::Files){
        SetAction(Button::Y,Action{"Select",[this]{TogglePackage();}});
        SetAction(Button::X,Action{"Select all",[this]{SelectAllPackages();}});
        SetAction(Button::START,Action{"Review",[this]{ReviewBatch();}});
        SetAction(Button::SELECT,Action{"Refresh",[this]{Refresh();}});
    }else if(page==Page::Queue){
        SetAction(Button::Y,Action{"Remove",[this]{if(selected<Count()){const auto path=packages.Entries()[selected].path;packages.Remove(path);selected=Count()?std::min(selected,Count()-1):0;}}});
        SetAction(Button::L,Action{"Earlier",[this]{if(packages.Move(selected,false))--selected;}});
        SetAction(Button::R,Action{"Later",[this]{if(packages.Move(selected,true))++selected;}});
        SetAction(Button::START,Action{"Install",[this]{ChooseBatchDestination();}});
    }else if(page==Page::Results){
        SetAction(Button::START,Action{"Remaining",[this]{ReviewBatch();}});
    }else{
        SetAction(Button::X,page==Page::Home || page==Page::More?Action{[this]{Refresh();}}:Action{"Refresh",[this]{Refresh();}});
        if(page!=Page::More)SetAction(Button::SELECT,Action{"Options",[this]{Updates();}});
        SetAction(Button::START,Action{"Exit",App::Exit});
    }
}
void Menu::TogglePackage(){
    if(selected>=files.size() || files[selected].directory)return;
    const auto& f=files[selected];
    if(packages.Contains(f.path))packages.Remove(f.path);
    else if(!packages.Add({f.name,f.path,f.size}))Report(nexus::Invalid,"Select a non-empty NSP, XCI, NSZ or XCZ. A queue holds up to 256 packages.");
}
void Menu::SelectAllPackages(){
    std::vector<nexus::batch::Entry> entries;
    for(const auto& f:files){nexus::batch::Entry e{f.name,f.path,f.size};if(!f.directory && nexus::batch::ValidEntry(e))entries.push_back(std::move(e));}
    if(entries.empty()){App::Notify("No installable packages in this folder.");return;}
    if(std::ranges::all_of(entries,[&](const auto& e){return packages.Contains(e.path);})){
        for(const auto& e:entries)packages.Remove(e.path);
    }else if(!packages.AddAll(entries))Report(nexus::Invalid,"The whole folder was not added: queue limit is 256 packages. Select fewer files.");
}
void Menu::ReviewBatch(){
    if(packages.Empty()){App::Notify("Select packages with Y, or use X for all packages in the current folder.");return;}
    page=Page::Queue;selected=0;actions_dirty=true;
}
void Menu::QueueDetails(){
    if(selected>=Count())return;
    const auto& e=packages.Entries()[selected];
    App::Push<OptionBox>("Package "+std::to_string(selected+1)+"\n\n"+e.name+"\n\n"+e.path+"\n\nSource size: "+utils::formatSizeStorage(e.size)+"\nUse L/R to change installation order.","Back","OK",0,[](auto){});
}
void Menu::ChooseBatchDestination(){
    if(packages.Empty()){App::Notify("The package queue is empty.");return;}
    if(App::IsApplet()){Report(nexus::Unavailable,"Use full-memory homebrew mode before installing, not Album applet mode.");return;}
    const auto entries=packages.Entries();
    auto sidebar=std::make_unique<Sidebar>("Where to install?",std::to_string(entries.size())+" selected files",Sidebar::Side::RIGHT);
    for(auto target:{Target::Sd,Target::Internal,Target::Usb})
        sidebar->Add<SidebarEntryCallback>(nexus::TargetName(target),[this,target,entries]{StartBatch(target,entries);});
    App::Push(std::move(sidebar));
}
void Menu::StartBatch(Target target,const std::vector<nexus::batch::Entry>& entries){
    if(App::IsApplet()){Report(nexus::Unavailable,"Use full-memory homebrew mode before installing, not Album applet mode.");return;}
    const bool from_usb=usb_source;const u64 source_generation=generation;
    const auto message="Install "+std::to_string(entries.size())+" package(s) to "+nexus::TargetName(target)+"?\n\n"+
        "All selected packages use this destination and the reviewed queue order. Select base, update, then DLC.\n\nThe queue stops on the first error or cancellation. Completed installs stay installed; a stopped package may be incomplete. Source files are kept.\n\nKeep the drive connected and other installers closed.";
    App::Push<OptionBox>(message,"Cancel","Install",0,[this,target,entries,from_usb,source_generation](auto choice){
        if(!choice || *choice!=1)return;
        auto result=std::make_shared<nexus::batch::Report>(entries,target);
        App::Push<ProgressBox>(0,"Prepare batch",nexus::TargetName(target),[result,from_usb,source_generation](ProgressBox* p){
            return nexusapp::InstallBatch(p,from_usb,source_generation,*result);
        },[this,result,from_usb](Result rc){
            result->result=rc;batch_result=result;batch_source=from_usb?"USB":"SD";
            for(const auto& row:result->items)if(row.state==nexus::batch::State::Installed)packages.Remove(row.entry.path);
            // Failed and unattempted items remain selected; never retry successes automatically.
            page=Page::Results;selected=0;actions_dirty=true;GetPolledData(true);
        });
    });
}
void Menu::BatchResultDetails(){
    if(!batch_result || selected>=batch_result->items.size())return;
    const auto& row=batch_result->items[selected];
    const auto text=std::string(nexus::batch::StateName(row.state))+"\n\n"+row.entry.name+"\n\n"+row.entry.path+
        "\n\nDestination: "+nexus::TargetName(batch_result->target)+
        (row.detail.empty()?std::string{}:"\n\n"+row.detail)+
        "\nSource file kept. Failed/cancelled packages may be partly installed.\nDetails: switch/nexusmanager/batch-last.log";
    if(row.result)Report(row.result,text);else App::Push<OptionBox>(text,"Back","OK",0,[](auto){});
}
void Menu::GameActions(bool primary_move){
    if(App::IsApplet()){Report(nexus::Unavailable,"Open Nexus Manager in full-memory mode (hold R while starting a game), not through Album.");return;}
    if(selected>=games.size())return;
    const auto game=games[selected];
    if(!game.homebrew_path.empty()){
        HomebrewActions({game.name,game.homebrew_path,game.file_size,false},game.generation);return;
    }
    // Always refresh the selected title, even when an earlier location is
    // displayed. A cached row is never proof the same USB media is present.
    auto checked=std::make_shared<nexusapp::Game>();
    App::Push<ProgressBox>(0,"Checking this game",game.name,[game,checked](ProgressBox* p){
        return nexusapp::InspectGame(p,game.id,*checked);
    },[this,game,checked,primary_move](Result rc){
        if(R_FAILED(rc)){
            if(!primary_move){DeleteGameOptions(game);App::Notify("Storage check did not finish. You can still review a deletion; no files have changed.");}
            else Report(rc,"Could not check this game's storage. Reconnect the drive and try again. Nothing was changed.");
            return;
        }
        if(page!=Page::Installed)return;
        const auto row=std::find_if(games.begin(),games.end(),[&](const auto& g){return g.id==game.id;});
        if(row==games.end())return;
        checked->name=game.name;checked->display_version=game.display_version;
        checked->installed_bytes=game.installed_bytes;checked->size_known=game.size_known;
        checked->base_version=game.base_version;checked->update_version=game.update_version;
        checked->has_base=game.has_base;checked->has_update=game.has_update;checked->dlc_count=game.dlc_count;
        *row=*checked;
        actions_dirty=true;
        if(primary_move)MoveGameOptions(*checked,true,true);else ShowGameActions(*checked);
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::ShowGameActions(const nexusapp::Game& game){
    auto sidebar=std::make_unique<Sidebar>(game.name,StorageLabel(game),Sidebar::Side::RIGHT);
    sidebar->Add<SidebarEntryCallback>(PrimaryLabel(game),[this,game]{MoveGameOptions(game,true,true);});
    if(game.forwarder_path.empty())sidebar->Add<SidebarEntryCallback>("Choose another destination",[this,game]{MoveGameOptions(game,true);});
    sidebar->Add<SidebarEntryCallback>("Copy game (keep original)",[this,game]{MoveGameOptions(game,false);});
    sidebar->Add<SidebarEntryCallback>("Dump game to NSP",[this,game]{
        auto sources=std::make_unique<Sidebar>("Dump to NSP","Choose where the installed game is stored",Sidebar::Side::RIGHT);
        bool available=false;
        for(auto source:{Target::Sd,Target::Internal,Target::Usb})if(game.bytes[static_cast<size_t>(source)]){
            available=true;
            sources->Add<SidebarEntryCallback>(std::string("From ")+nexus::TargetName(source),[this,game,source]{
                auto destinations=std::make_unique<Sidebar>("Save NSP backup","Choose where to save the backup",Sidebar::Side::RIGHT);
                for(auto destination:{Target::Sd,Target::Usb})destinations->Add<SidebarEntryCallback>(std::string("Save to ")+nexus::TargetName(destination),[this,game,source,destination]{
                App::Push<OptionBox>("Back up "+game.name+"?\n\nRead from: "+nexus::TargetName(source)+
                    "\nSave to: "+nexus::TargetName(destination)+" /dumps/NSP/\n\nIncludes the game, updates and extra content on this source, not saves. Originals stay installed. Keep storage connected and other installers closed.\n\nThe saved NSP is fully checked. "+
                    (destination==Target::Usb?"USB backups of 4 GiB or more need exFAT. ":"Large files work on FAT32 SD. ")+
                    "Missing/personalized licenses stop this version. Keep backups private.",
                    "Cancel","Dump to NSP",0,[this,game,source,destination](auto choice){
                        if(!choice || *choice!=1)return;
                        auto report=std::make_shared<nexusapp::DumpReport>();
                        App::Push<ProgressBox>(0,std::string("Dump NSP to ")+nexus::TargetName(destination),game.name,[game,source,destination,report](ProgressBox* p){
                            return nexusapp::DumpGameToNsp(p,game.id,game.name,source,destination,*report);
                        },[this,report](Result rc){
                            if(R_FAILED(rc)){Report(rc,report->Message());return;}
                            App::Push<OptionBox>(report->Message(),"A / Close",[](auto){});
                        });
                    });
                });
                App::Push(std::move(destinations));
            });
        }
        if(!available){App::Push<OptionBox>("No complete installed location was found. Connect the game storage and refresh Installed games.","A / Close",[](auto){});return;}
        App::Push(std::move(sources));
    });
    sidebar->Add<SidebarEntryCallback>("Delete game",[this,game]{DeleteGameOptions(game);},"Remove this game, updates and DLC from selected storage. Saved progress is kept.");
    sidebar->Add<SidebarEntryCallback>("Check and repair",[this,game]{RecoverUsbIndexes(game.id);});
    if(!game.forwarder_path.empty())sidebar->Add<SidebarEntryCallback>("Forwarder information",[game]{
        App::Push<OptionBox>("HOME-menu forwarder\n\n"+game.forwarder_path+"\n\nMove/Copy game transfers the full port folder, including every file and subfolder, and creates a shortcut for SD or USB. Known separate data folders are included automatically.","Back","OK",0,[](auto){});
    });
    sidebar->Add<SidebarEntryCallback>("Refresh game list",[this]{Refresh();});
    App::Push(std::move(sidebar));
}
void Menu::BeginTransfer(const nexusapp::Game& game,Target source,Target target,bool move){
    if(!game.location_checked || !nexus::ValidMove(source,target) || !game.bytes[static_cast<size_t>(source)]){
        Report(nexus::Invalid,"Choose the game again so its storage can be checked. Nothing was changed.");return;
    }
    const auto route=std::string(nexus::TargetName(source))+" to "+nexus::TargetName(target);
    const auto size=utils::formatSizeStorage(game.bytes[static_cast<size_t>(source)]);
    const auto text=game.name+"\n\n"+route+"  |  "+size+"\nIncludes game, updates and extra content on "+nexus::TargetName(source)+".\n\n"+
        (move?"The original is removed only after the destination copy is checked.":"Both copies will be kept.")+
        " Your saves stay unchanged.\n\nClose the game and other installers. Keep storage connected until finished.";
    App::Push<OptionBox>(text,"Cancel",move?"Move game":"Copy game",0,[this,game,source,target,route,move](auto choice){
        if(!choice || *choice!=1)return;
        auto report=std::make_shared<nexusapp::TransferReport>();
        App::Push<ProgressBox>(0,std::string(move?"Moving ":"Copying ")+route,game.name,[game,source,target,move,report](ProgressBox* p){
            return nexusapp::TransferGame(p,game.id,source,target,move,*report);
        },[this,route,report](Result rc){
            refresh_pending=true;
            const auto text=route+"\n\n"+report->Message();
            if(R_FAILED(rc)){Report(rc,text);return;}
            App::Push<OptionBox>(text,"Done",[](auto){});
        });
    });
}
void Menu::MoveGameOptions(const nexusapp::Game& game,bool move,bool primary){
    if(!game.forwarder_path.empty()){PortTransferOptions(game,move);return;}
    const auto preferred=view::PrimaryRoute(game.bytes,game.location_checked);
    if(primary && preferred.available){BeginTransfer(game,preferred.source,preferred.destination,move);return;}
    auto routes=std::make_unique<Sidebar>(move?"Move game":"Copy game","Choose the source and destination",Sidebar::Side::RIGHT);
    bool available=false;
    if(game.location_checked)for(auto source:{Target::Sd,Target::Usb,Target::Internal})if(game.bytes[static_cast<size_t>(source)]){
        for(auto target:{Target::Usb,Target::Sd,Target::Internal})if(nexus::ValidMove(source,target)){
            available=true;
            const auto route=std::string(nexus::TargetName(source))+" to "+nexus::TargetName(target);
            routes->Add<SidebarEntryCallback>(route,[this,game,source,target,move]{BeginTransfer(game,source,target,move);});
        }
    }
    if(!available){App::Push<OptionBox>("This game's storage is unavailable. Connect the drive, then refresh Installed games. Nothing was changed.","Close",[](auto){});return;}
    App::Push(std::move(routes));
}
void Menu::PortTransferOptions(const nexusapp::Game& game,bool move){
    if(!game.forwarder_migratable){Report(nexus::Invalid,"This forwarder's launch settings could not be read safely. Its files were kept. Create a recognised NRO shortcut before transferring this port.");return;}
    auto plan=std::make_shared<nexus::port::Plan>();auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Reading port folder",game.name,[game,move,plan,detail](ProgressBox* p){
            return nexusapp::PreparePortTransfer(p,game,move,*plan,*detail);
        },[this,plan,detail](Result rc){
            if(R_FAILED(rc)){Report(rc,*detail+"\n\nCould not prepare the transfer. Check that the port's source files and USB storage are available. Files were not changed.");return;}
            App::Push<OptionBox>(*detail,"Cancel",plan->move?"Move port":"Copy port",0,[this,plan](auto choice){
                if(!choice||*choice!=1)return;
                auto report=std::make_shared<nexus::port::Report>();
                App::Push<ProgressBox>(0,plan->move?"Moving port":"Copying port",plan->name,[plan,report](ProgressBox* p){
                    return nexusapp::TransferPort(p,*plan,*report);
                },[this,plan,report](Result result){refresh_pending=true;const auto message=report->Message(plan->move);
                    if(R_FAILED(result))Report(result,message);else App::Push<OptionBox>(message,"A / Close",[](auto){});
                });
            });
        });
}
void Menu::RefreshBadges(){
    App::Push<ProgressBox>(0,"Refreshing HOME icons","Checking game locations; this can take longer",[](ProgressBox* p){
        return nexusapp::RefreshHomeBadges(p);
    },[this](Result rc){
        Report(rc,R_SUCCEEDED(rc)?"HOME icon refresh finished. Some icons may remain pending if their storage is unavailable. Game files were not changed.":
            "HOME icon refresh stopped. Game files were not changed.");
    });
}
void Menu::Updates(){
    auto options=std::make_unique<Sidebar>("Help & recovery","Guides, USB checks, recovery and app updates",Sidebar::Side::RIGHT,1280.f);
    options->Add<SidebarEntryCallback>("How to move a game",[]{
        App::Push<OptionBox>("1. Open Installed games and highlight a game.\n2. Press A for Manage, then choose Move. Its storage is checked first.\n3. Review the destination and confirm.\n\nOne bar covers copying, checking and finishing. The original is removed only after the copy passes its checks. Save data and shared files are kept.\n\nFor a different destination or to keep both copies, press A for Manage.\n\nB requests cancellation. Keep storage connected and wait for the job to stop.","Close",[](auto){});
    });
    options->Add<SidebarEntryCallback>("How to use Nexus Manager",[]{
        App::Push<OptionBox>("Installed games: select a game, then A to manage, move or delete it. Left/right changes page.\n\nInstall games: find an install file on SD or USB, then choose its destination. The original package is kept.\n\nFile manager: copy, cut, paste or delete files and folders on SD or USB. X opens actions; Storage changes drive.\n\nMore: PC transfer, cartridges, homebrew, saves, mods, help and recovery.\n\nUse B to go back and + to exit. Keep storage connected during transfers.","Close",[](auto){});
    });
    options->Add<SidebarEntryCallback>("Clean up unfinished installs",[this]{CleanupTargets();},"Review leftover game files before choosing anything to remove. Saved progress is kept.");
    options->Add<SidebarEntryCallback>("Resume interrupted USB installs",[this]{ResumeInstalls();},"Check saved installation checkpoints and retry with the original game package.");
    options->Add<SidebarEntryCallback>("Check USB connection",[this]{
        auto report=std::make_shared<nexus::health::Report>();
        App::Push<ProgressBox>(0,"Checking USB connection","No game files will be changed",[report](ProgressBox* p){
            return nexusapp::CheckUsbHealth(p,*report);
        },[this,report](Result rc){
            if(R_FAILED(rc)){Report(rc,report->detail);return;}
            if(!report->offer_scan){App::Push<OptionBox>(report->detail,"Back",[](auto){});return;}
            App::Push<OptionBox>(report->detail,"Back","Repair library",0,[this](auto choice){
                if(choice && *choice==1)RecoverUsbIndexes();
            });
        },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    });
    options->Add<SidebarEntryCallback>("Show USB labels on HOME icons",[this]{
        App::Push<ProgressBox>(0,"Enable HOME USB badges","Prepared on SD; game files stay unchanged",[](ProgressBox*){return nexusapp::EnableHomeBadges(true);},[this](Result rc){
            if(R_FAILED(rc)){Report(rc,"Could not enable HOME badges.");return;}
            RefreshBadges();
        });
    });
    options->Add<SidebarEntryCallback>("Hide USB labels on HOME icons",[this]{
        App::Push<ProgressBox>(0,"Disable HOME USB badges","Keep the cache; use original icons",[](ProgressBox*){return nexusapp::EnableHomeBadges(false);},[this](Result rc){
            Report(rc,R_SUCCEEDED(rc)?"Badges disabled. Restart the console if HOME retains old icons.":"Could not disable badges.");
        });
    });
    options->Add<SidebarEntryCallback>("Refresh HOME USB icons",[this]{RefreshBadges();});
    options->Add<SidebarEntryCallback>("Repair USB library",[this]{RecoverUsbIndexes();},"Check USB games and rebuild their links for this SD card and system.");
    options->Add<SidebarEntryCallback>("Network locations (SMB / NFS)",[]{devoptab::DisplayNetworkLocations();});
    options->Add<SidebarEntryCallback>("Check for Manager updates",[this]{CheckForUpdates();},"Check for a newer Nexus Manager, then choose whether to download and install it.");
    options->Add<SidebarEntryCallback>("About Nexus Manager",[]{
        App::Push<OptionBox>(std::string("Nexus Manager ")+nexus::updates::Version+"\n\nIndependent GPL app based on Nexus USB Loader and Sphaira. No DBI binary, code or artwork is included.\n\nManager checks for updates at startup. Downloads only start when you choose to update. You can also check in Help & recovery. Firmware and settings stay unchanged.","Close",[](auto){});
    });
    App::Push(std::move(options));
}
void Menu::CheckForUpdates(){
    // The manual check takes over this launch's prompt and network session.
    startup_update_handled=true;
    nexus::updates::StopStartupCheck();
    auto release=std::make_shared<nexus::updates::Release>();
    auto detail=std::make_shared<std::string>();
    App::Push<ProgressBox>(0,"Checking Manager updates","TechRepairs4U / Nexus-Manager",[release,detail](ProgressBox* p){
        return nexus::updates::Check(p,*release,*detail);
    },[this,release,detail](Result rc){
        if(R_FAILED(rc)){Report(rc,*detail);return;}
        if(nexus::updates::VersionCode(release->version)<=nexus::updates::VersionCode(nexus::updates::Version)){
            App::Push<OptionBox>("Nexus Manager is up to date. Nothing was changed.","Close",[](auto){});return;
        }
        ShowUpdatePrompt(*release);
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::OpenUsbRepair(){
    if(!HasFocus()||ShouldPop()||!nexus::lifecycle::CanPoll())return;
    startup_usb_handled=true;
    nexusapp::DisplayUsbImport();
}
bool Menu::PollUsbLibraryStartup(){
    if(startup_usb_handled||!HasFocus()||ShouldPop()||!nexus::lifecycle::CanPoll())return false;
    if(App::IsApplet()){startup_usb_handled=true;return false;}
    if(!startup_usb_started){startup_usb_started=true;nexusapp::StartUsbLibraryStartupCheck();}
    // A completed scan waits for an idle library/Home page. It never opens
    // over a browser, operation, another dialog or a pending library refresh.
    if(refresh_pending||(page!=Page::Home&&page!=Page::Installed))return false;
    nexusapp::UsbLibraryStartupResult result;
    if(!nexusapp::TakeUsbLibraryStartupCheck(result))return false;
    startup_usb_handled=true;
    if(!result.pending&&R_SUCCEEDED(result.result)&&!result.affected_games)return false;
    std::string text;
    if(result.pending){
        text="A USB game repair needs recovery\n\nAn earlier operation left a saved recovery journal. Review the backup before adding, installing or moving games.\n\nYour USB game files and saves remain in place. No automatic repair has been made.";
    }else if(R_FAILED(result.result)){
        text="The USB check could not finish\n\n"+(result.detail.empty()?std::string("Review a fresh scan to see what needs attention. Keep any existing repair backup folders."):result.detail)+
            "\n\nNo automatic repair has been made.";
        char code[40]{};snprintf(code,sizeof(code),"\n\nDiagnostic code: 0x%08X",result.result);text+=code;
    }else{
        text="USB games need attention\n\nGames needing attention: "+std::to_string(result.affected_games)+
            "\nGames ready: "+std::to_string(result.repairable_games)+
            "\nNeed checking: "+std::to_string(result.blocked_games)+
            "\n\nEach game is checked together with its updates and DLC. Review a fresh scan, then choose whether to repair the ready families. No game registrations have been changed.";
        if(result.missing_ticket_games)text+="\n\n"+std::to_string(result.missing_ticket_games)+" game families need permissions on this system. Review their details for preparation or original-backup instructions.";
    }
    text+="\n\nYou can also open Installed games and choose Repair USB games later.";
    auto consumed=std::make_shared<bool>();
    App::Push<OptionBox>(text,"Not now",result.pending?"Review recovery":R_FAILED(result.result)?"Review USB check":"Review repairs",0,[consumed](auto choice){
        if(*consumed)return;*consumed=true;
        if(choice&&*choice==1)nexusapp::DisplayUsbImport();
    });
    return true;
}
bool Menu::PollStartupUpdate(){
    if(!HasFocus() || !nexus::lifecycle::CanPoll())return false;
    if(!startup_update_started){
        startup_update_started=true;
        nexus::updates::StartStartupCheck(envIsNso()?nexus::updates::Artifact::Nsp:nexus::updates::Artifact::Nro);
    }
    // Never interrupt a browser, transfer, repair or another dialog. If the
    // check finishes while navigating, keep its result until Home is visible.
    if(startup_update_handled || page!=Page::Home)return false;
    nexus::updates::Release release;
    if(!nexus::updates::TakeStartupRelease(release))return false;
    startup_update_handled=true;
    ShowUpdatePrompt(release);
    return true;
}
void Menu::ShowUpdatePrompt(const nexus::updates::Release& available){
    auto release=std::make_shared<nexus::updates::Release>(available);
    auto detail=std::make_shared<std::string>();
    const bool standalone=release->kind==nexus::updates::Artifact::Nsp;
    const auto text="A Nexus Manager update is available!\n\nVersion "+release->version+
        (release->preview?" (test release)":"")+"\n\n"+(standalone?
        "Download and install the update for your HOME app? Manager will reopen automatically when it is finished.":
        "Download and install the update? Your current NRO will be kept as a backup, then Manager will reopen automatically.")+
        "\n\nKeep the SD card inserted until Manager reopens. Choose Not now to carry on. Your games, settings and firmware stay unchanged.";
    App::Push<OptionBox>(text,"Not now","Update and restart",0,[this,release,detail](auto choice){
        if(!choice || *choice!=1)return;
        App::Push<ProgressBox>(0,"Nexus Manager update","Downloading, checking and installing",[release,detail](ProgressBox* p){
            return nexus::updates::Install(p,*release,*detail);
        },[this,detail](Result result){
            if(R_FAILED(result)){Report(result,*detail);return;}
            const auto restarted=nexus::updates::RestartUpdatedManager(*detail);
            if(R_FAILED(restarted))Report(restarted,*detail);
        });
    });
}
// Keep the user in the report/exit flow after index publication. B on an issue
// returns to the summary, never to a stale Installed view or another installer.
static void ShowRecoveryResult(std::shared_ptr<nexusapp::RecoveryPlan> plan,Result result,size_t page=SIZE_MAX){
    if(page==SIZE_MAX){
        App::Push<OptionBox>(nexusapp::RecoverySummary(*plan,result,result==Result_TransferCancelled),
            "Details",plan->published?"Exit app":"Done",1,[plan,result](auto choice){
                if(choice && *choice==0)ShowRecoveryResult(plan,result,0);
                else if(plan->published)App::Exit();
            });
        return;
    }
    // Technical diagnostics are optional, never the default result. B always
    // returns to the summary; after publication that summary can only exit.
    const auto text=page==0?plan->detail:nexusapp::RecoveryIssuePage(*plan,page-1);
    App::Push<OptionBox>(text,page?"Previous":"Summary",
        page<plan->issues.size()?"Next detail":"Summary",1,[plan,result,page](auto choice){
            if(!choice)ShowRecoveryResult(plan,result);
            else if(*choice==0)ShowRecoveryResult(plan,result,page?page-1:SIZE_MAX);
            else ShowRecoveryResult(plan,result,page<plan->issues.size()?page+1:SIZE_MAX);
        });
}
void Menu::ResumeInstalls(){
    auto rows=std::make_shared<std::vector<nexusapp::InstallReceipt>>();
    App::Push<ProgressBox>(0,"Checking install checkpoints","Inspecting receipts; no game changes",[rows](ProgressBox*){return nexusapp::PendingInstalls(*rows);},[this,rows](Result rc){
        if(R_FAILED(rc)){Report(rc,"Recovery checkpoint unreadable. No game files changed. Keep completion-p3 for diagnosis; do not assume installs are complete.");return;}
        if(rows->empty()){App::Notify("No unfinished USB install checkpoints.");return;}
        auto sidebar=std::make_unique<Sidebar>("Interrupted USB installs","Choose an installation to check and resume",Sidebar::Side::RIGHT,1280.f);
        for(const auto& row:*rows)sidebar->Add<SidebarEntryCallback>(row.path,[this,row]{
            App::Push<OptionBox>("Resume by checking the original package again?\n\n"+std::string(row.usb_source?"USB:":"SD:")+row.path+
                "\n\nConnect the original source and destination. Every reused NCA is rehashed; incomplete placeholders are not trusted. Saves and other games are untouched. Changed/bad files stop this attempt.","Cancel","Recheck + resume",0,[this,row](auto choice){
                if(!choice || *choice!=1)return;
                auto gen=std::make_shared<u64>();
                App::Push<ProgressBox>(0,"Check resume source",row.path,[row,gen](ProgressBox*)->Result{
                    if(row.usb_source){nexus::Client c;nexus::Status s{};R_TRY(c.Query(s));R_UNLESS(s.Available(),nexus::Unavailable);*gen=s.generation;}return 0;
                },[this,row,gen](Result check){
                    if(R_FAILED(check)){Report(check,"USB source is not ready. Nothing resumed.");return;}
                    packages.Clear();usb_source=row.usb_source;generation=*gen;
                    StartBatch(Target::Usb,{{row.path,row.path,row.source_size}});
                });
            });
        });
        App::Push(std::move(sidebar));
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::RecoverUsbIndexes(u64 only_application){
    if(App::IsApplet()){Report(nexus::Unavailable,"Open Nexus Manager in full-memory mode (hold R while starting a game), not through Album, then try again.");return;}
    App::Push<OptionBox>(nexusapp::RecoveryConfirmation(only_application!=0),"Cancel","Start",0,[only_application](auto choice){
        if(!choice || *choice!=1)return;
        auto plan=std::make_shared<nexusapp::RecoveryPlan>();
        App::Push<ProgressBox>(0,only_application?"Check and repair":"Repair USB library","Game files and saves are kept",[plan,only_application](ProgressBox* p)->Result{
            R_TRY(nexusapp::ScanUsbRecovery(p,*plan,only_application));
            // Restoring an interrupted whole-library transaction requires its
            // own explicit confirmation; a per-game check must never do it.
            if(plan->pending)return 0;
            if(plan->ready)return nexusapp::ApplyUsbRecovery(p,*plan);
            p->SetOverallTransfer(10000,10000,"Check finished",true);return 0;
        },[plan](Result rc){
            if(R_FAILED(rc) || !plan->pending){ShowRecoveryResult(plan,rc);return;}
            App::Push<OptionBox>("An earlier repair was interrupted\n\nRestore the saved USB library backup before trying another repair? Game files and saves will not be changed.\n\nAfter restoring, close Nexus Manager and fully restart the Switch, then run Repair USB library again.","Not now","Restore backup",0,[plan](auto confirm){
                if(!confirm || *confirm!=1)return;
                App::Push<ProgressBox>(0,"Restoring library backup","Keep storage connected",[plan](ProgressBox* p){return nexusapp::ApplyUsbRecovery(p,*plan);},[plan](Result result){ShowRecoveryResult(plan,result);});
            });
        },ProgressBoxOption::None,nexus::safety::Operation::MayWrite);
    });
}
void Menu::DeleteGameOptions(const nexusapp::Game& game){
    auto menu=std::make_unique<Sidebar>("Delete game",game.name+" - choose which copy to remove. Saved progress is kept.",Sidebar::Side::RIGHT,1280.f);
    // Include unavailable/incomplete locations: the preview determines what
    // actually exists, not the optimistic size cached by the library row.
    for(auto target:{Target::Sd,Target::Internal,Target::Usb})menu->Add<SidebarEntryCallback>(std::string("Delete ")+nexus::TargetName(target)+" copy",[this,game,target]{
        nexusapp::CleanupItem item;item.kind=nexusapp::CleanupKind::Game;item.target=target;item.application=game.id;item.name=game.name;
        item.reason="Base game, updates and DLC on this storage. Copies on other storage and saved progress are kept.";ReviewRemoval(item);
    },true,"Review the exact files first. The other storage copy stays installed.");
    App::Push(std::move(menu));
}
void Menu::ReviewRemoval(const nexusapp::CleanupItem& item){
    auto plan=std::make_shared<nexusapp::GameRemovalPlan>();
    App::Push<ProgressBox>(0,"Review deletion",item.name,[item,plan](ProgressBox* p){return nexusapp::PreviewGameRemoval(p,item,*plan);},[this,plan,item](Result rc){
        if(R_FAILED(rc)){Report(rc,!plan->detail.empty()?plan->detail:"Could not safely prepare this deletion. No files were removed. The item may have changed, have unreadable metadata, or have no files on this storage. Close other installers and scan again. The running app cannot delete itself.");return;}
        const auto text=item.name+"\n\n"+std::string(item.kind==nexusapp::CleanupKind::Ticket?"Current system ticket":nexus::TargetName(item.target))+" | "+std::to_string(plan->files)+(item.kind==nexusapp::CleanupKind::Ticket?" ticket":" files | "+utils::formatSizeStorage(plan->bytes))+
            "\n\n"+item.reason+"\n\n"+plan->detail+"\n\nClose the game, downloads and other installers first. Deletion cannot be undone.";
        App::Push<OptionBox>(text,item.kind==nexusapp::CleanupKind::Ticket?"Keep ticket":"Keep files","Delete",0,[this,plan](auto choice){
            if(!choice||*choice!=1)return;auto report=std::make_shared<nexusapp::GameRemovalReport>();
            App::Push<ProgressBox>(0,"Deleting reviewed files",plan->item.name,[plan,report](ProgressBox* p){return nexusapp::ApplyGameRemoval(p,*plan,*report);},[this,report](Result result){
                refresh_pending=page==Page::Installed;GetPolledData(true);
                if(page==Page::Cleanup){cleanup_items.clear();cleanup_detail="Press X to scan again for remaining items.";selected=0;actions_dirty=true;}
                if(R_FAILED(result))Report(result,report->detail);else App::Push<OptionBox>(report->detail,"Done",[](auto){});
            });
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::ScanCleanupTarget(Target target){
    auto scan=std::make_shared<nexusapp::CleanupScan>();
    App::Push<ProgressBox>(0,"Scan cleanup candidates",nexus::TargetName(target),[target,scan](ProgressBox* p){return nexusapp::ScanCleanup(p,target,*scan);},[this,target,scan](Result rc){
        if(R_FAILED(rc)){Report(rc,"Cleanup scan did not finish. Nothing was deleted. Check the storage connection and close other installers, then retry.");return;}
        cleanup_items=std::move(scan->items);cleanup_detail=std::move(scan->detail);cleanup_target=target;cleanup_tickets=false;page=Page::Cleanup;selected=0;actions_dirty=true;
        App::Push<OptionBox>(cleanup_detail,"Review items",[](auto){});
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::DeleteAllCleanup(){
    if(cleanup_items.empty())return;
    const auto items=cleanup_items;auto batch=std::make_shared<nexusapp::CleanupBatchPlan>();
    App::Push<ProgressBox>(0,"Review all cleanup items","Nothing is being removed yet",[items,batch](ProgressBox* p){return nexusapp::PreviewCleanupBatch(p,items,*batch);},[this,batch](Result rc){
        if(R_FAILED(rc)){Report(rc,"The bulk review did not finish. Nothing was deleted. Review individual items for the failed check.");return;}
        if(!batch->ready){App::Push<OptionBox>("None of these items could be prepared for deletion. Nothing was changed. Open an individual item to see which check failed.","Back",[](auto){});return;}
        const auto noun=batch->tickets?"unused tickets":"cleanup items";
        std::string text="Delete all "+std::to_string(batch->ready)+" reviewed "+noun+"?\n\n";
        if(!batch->tickets)text+=std::string(nexus::TargetName(batch->entries.front().item.target))+" | "+std::to_string(batch->files)+" files | "+utils::formatSizeStorage(batch->bytes)+"\n\nIncludes the displayed games marked Ticket missing or Not on HOME, if any, with their updates and DLC.\n\n";
        else text+="These permissions may belong to uninstalled purchases. Keep them if you plan to reinstall or cannot restore them.\n\n";
        text+=std::to_string(batch->skipped)+" item(s) cannot be verified and will be skipped. Changed items are also skipped. Saves and protected files are kept. Deletion cannot be undone.";
        App::Push<OptionBox>(text,"Keep everything","Delete all",0,[this,batch](auto choice){
            if(!choice||*choice!=1)return;auto report=std::make_shared<nexusapp::CleanupBatchReport>();
            App::Push<ProgressBox>(0,"Deleting reviewed cleanup items","Keep storage connected",[batch,report](ProgressBox* p){return nexusapp::ApplyCleanupBatch(p,*batch,*report);},[this,report](Result result){
                cleanup_items.clear();cleanup_detail="Press X to scan again for remaining items.";selected=0;actions_dirty=true;GetPolledData(true);
                if(R_FAILED(result))Report(result,report->detail);else App::Push<OptionBox>(report->detail,"Done",[](auto){});
            });
        });
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::ScanTickets(){
    auto scan=std::make_shared<nexusapp::CleanupScan>();
    App::Push<ProgressBox>(0,"Check unused tickets","Checking all storage before offering removal",[scan](ProgressBox* p){return nexusapp::ScanUnusedTickets(p,*scan);},[this,scan](Result rc){
        if(R_FAILED(rc)){Report(rc,scan->detail.empty()?"Ticket checks did not finish. Nothing was removed.":scan->detail);return;}
        cleanup_items=std::move(scan->items);cleanup_detail=std::move(scan->detail);cleanup_tickets=true;page=Page::Cleanup;selected=0;actions_dirty=true;
        App::Push<OptionBox>(cleanup_detail,"Review tickets",[](auto){});
    },ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
}
void Menu::CleanupTargets(){
    if(App::IsApplet()){Report(nexus::Unavailable,"Open the Nexus Manager HOME app or use full-memory mode to manage game content.");return;}
    auto menu=std::make_unique<Sidebar>("Cleanup","Choose storage to scan. You review every deletion. Saves are kept.",Sidebar::Side::RIGHT,1280.f);
    for(auto target:{Target::Sd,Target::Internal,Target::Usb})menu->Add<SidebarEntryCallback>(nexus::TargetName(target),[this,target]{ScanCleanupTarget(target);},true,"Review leftover files, unfinished installs and games missing tickets.");
    menu->Add<SidebarEntryCallback>("Unused tickets (this system)",[this]{ScanTickets();},true,"Find permissions with no installed content. Keep tickets used by SD, internal storage or USB games.");
    App::Push(std::move(menu));
}
void Menu::MoreOptions(){
    page=Page::More;selected=0;actions_dirty=true;
}
void Menu::ActivateMore(){
    if(selected==0){nexus::updates::StopStartupCheck();App::Push<extended::Menu>(extended::Mode::Transfer);}
    else if(selected==1)App::Push<extended::Menu>(extended::Mode::Cartridge);
    else if(selected==2){
        auto sources=std::make_unique<Sidebar>("Homebrew apps","Choose where your apps are stored",Sidebar::Side::RIGHT,1280.f);
        sources->Add<SidebarEntryCallback>("USB apps and shortcuts",[this]{packages.Clear();usb_source=true;generation=0;path="/";selected=0;page=Page::Homebrew;Refresh();},true,"Browse NRO apps on USB, open them in supported mode or create a HOME shortcut.");
        sources->Add<SidebarEntryCallback>("SD apps for startup timing",[this]{packages.Clear();usb_source=false;generation=0;path="/";selected=0;page=Page::Homebrew;Refresh();},true,"Browse SD apps and create timed shortcuts to compare startup performance.");
        App::Push(std::move(sources));
    }else if(selected==3)App::Push<managertools::Menu>(managertools::Mode::Saves);
    else if(selected==4){selected=0;page=Page::Mods;Refresh();}
    else if(selected==5)Updates();
}
void Menu::Activate(){
    if(App::IsApplet() && page!=Page::Home && page!=Page::More){Report(nexus::Unavailable,"Open Nexus Manager in full-memory mode (hold R while starting a game), not through Album.");return;}
    if(page==Page::Home){
        if(selected==0){page=Page::Installed;Refresh();}
        else if(selected==1){
            App::Push<InstallSourcesSidebar>([this](Sidebar& sources){
                sources.Add<SidebarEntryCallback>("SD card",[this]{ChooseSource(false);},true);
                sources.Add<SidebarEntryCallback>("USB drive",[this]{ChooseSource(true);},true);
            });
        }
        else if(selected==2)App::Push<managertools::Menu>(managertools::Mode::Volumes);
        else if(selected==3)CleanupTargets();
        else if(selected==4)devoptab::DisplayNetworkLocations();
        else if(selected==5)MoreOptions();
    }else if(page==Page::More)ActivateMore();
    else if(page==Page::Mods)ModActions();
    else if(page==Page::ModFolders && selected<files.size()){path=files[selected].path;Refresh();
    }else if(page==Page::Queue)QueueDetails();
    else if(page==Page::Results)BatchResultDetails();
    else if(page==Page::Installed)GameActions();
    else if((page==Page::Files || page==Page::Homebrew) && selected<files.size()){
        if(page==Page::Homebrew && files[selected].path.empty()){RunProbe();return;}
        auto item=files[selected];if(item.directory){path=item.path;selected=0;Refresh();}
        else if(!packages.Empty())ReviewBatch();
        else if((usb_source || page==Page::Homebrew) && nexus_vsd::Nro(item.path))HomebrewActions(item,generation,usb_source);
        else ChooseInstall(item);
    }else if(page==Page::Cleanup && selected<cleanup_items.size())ReviewRemoval(cleanup_items[selected]);
}
void Menu::Update(Controller* c,TouchInfo* touch){
    if(PollStartupUpdate())return;
    if(PollUsbLibraryStartup())return;
    if(actions_dirty)SetupActions();
    const auto previous_selection=selected;
    MenuBase::Update(c,touch);
    if(HasFocus() && touch->is_clicked){
        if(page==Page::Home){
            for(size_t i=0;i<view::HomeCount;++i){const auto r=view::HomeCard(i);if(touch->in_range({r.x,r.y,r.w,r.h})){selected=i;Activate();break;}}
        } else if(page==Page::More){
            for(size_t i=0;i<view::MoreCount;++i){const auto r=view::MoreCard(i);if(touch->in_range({r.x,r.y,r.w,r.h})){selected=i;Activate();break;}}
        } else if(page==Page::Files || page==Page::Homebrew || page==Page::Cleanup || page==Page::Queue || page==Page::Results || page==Page::Mods || page==Page::ModFolders){
            const size_t start=selected/7*7;
            for(size_t i=start;i<std::min(Count(),start+7);++i)if(touch->in_range({54,174+(i-start)*60.f,1172,56})){
                if(page==Page::Files && touch->in_range({54,174+(i-start)*60.f,58,56}) && !files[i].directory){selected=i;TogglePackage();}
                else if(selected==i)Activate();else selected=i;
                break;
            }
        } else if(page==Page::Installed) {
            if(touch->in_range({850,110,376,46})){OpenUsbRepair();return;}
            const auto start=view::PageStart(selected);
            for(size_t i=start;i<std::min(Count(),start+view::PageSize);++i){
                const auto r=view::Card(i);
                if(touch->in_range({r.x,r.y,r.w,r.h})){if(selected==i)GameActions();else selected=i;break;}
            }
        }
    }
    if(selected!=previous_selection)actions_dirty=true;
    if(refresh_pending && HasFocus()){refresh_pending=false;Refresh();}
    SetSubHeading(page==Page::Home?"Home":page==Page::More?"More":page==Page::Files?std::to_string(packages.Entries().size())+" selected":
        std::to_string(Count()?selected+1:0)+" / "+std::to_string(Count()));
    if(page==Page::More)SetTitleSubHeading("More / Tools, apps and help");
    else if(page==Page::Files)SetTitleSubHeading(std::string(usb_source?"USB: ":"SD: ")+path);
    else if(page==Page::Homebrew)SetTitleSubHeading(std::string(usb_source?"USB Homebrew  /  ":"SD timing apps  /  ")+path);
    else if(page==Page::Mods)SetTitleSubHeading("USB Mods  /  "+mod_folder);
    else if(page==Page::ModFolders)SetTitleSubHeading("Choose USB Atmosphere folder  /  X: Use folder");
    else if(page==Page::Installed)SetTitleSubHeading("Installed games  /  R Repair USB games  /  Left and right change page");
    else if(page==Page::Cleanup)SetTitleSubHeading("Cleanup / "+std::string(cleanup_tickets?"Unused tickets":nexus::TargetName(cleanup_target))+" / Saved progress is kept");
    else SetTitleSubHeading(std::string("Nexus Manager ")+nexus::updates::Version);
    PollIcon();
    if(page==Page::Installed && HasFocus()){
        const auto start=view::PageStart(selected),end=std::min(games.size(),start+view::PageSize);
        for(size_t i=start;i<end;++i){
            auto& game=games[i];const auto key=game.CacheKey();
            const std::vector<u8>* data=nullptr;
            if(!game.homebrew_path.empty()){
                QueueIcon(game);continue;
            }else{
                if(title_requested.insert(game.id).second)title::PushAsync(game.id);
                const auto* result=title::GetAsync(game.id);
                if(!result || result->status!=title::NacpLoadStatus::Loaded)continue;
                if(result->lang.name[0])game.name.assign(result->lang.name,strnlen(result->lang.name,sizeof(result->lang.name)));
                game.display_version=result->display_version;
                data=&result->icon;
            }
            if(!icons.contains(key) && data && !data->empty()){
                auto img=ImageLoadIcon(*data);
                if(!img.data.empty()){
                    if(game.badge_state>0){
                        if(img.w!=256 || img.h!=256)img=ImageResize(img.data,img.w,img.h,256,256);
                        nexus_badge::Overlay(img.data.data(),img.data.size(),256);
                    }
                    const auto handle=nvgCreateImageRGBA(App::GetVg(),img.w,img.h,0,img.data.data());
                    if(handle>0)icons[key]=handle;
                }
                break; // one decoded image per frame
            }
        }
        // Bound GPU icon memory regardless of library size.
        for(auto it=icons.begin();it!=icons.end();){
            bool visible=false;
            for(size_t i=start;i<end;++i)visible|=games[i].CacheKey()==it->first;
            if(!visible){nvgDeleteImage(App::GetVg(),it->second);icon_checked.erase(it->first);it=icons.erase(it);}else ++it;
        }
    }
}
void Menu::Draw(NVGcontext* vg,Theme* theme){
    MenuBase::Draw(vg,theme);
    const auto fg=theme->GetColour(ThemeEntryID_TEXT),muted=theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto accent=theme->GetColour(ThemeEntryID_TEXT_SELECTED);
    const auto name_line=[&](const Vec4& r,float size,const std::string& text,bool focus){
        if(!focus){BoundedLine(vg,r,size,fg,text);return;}
        nvgSave(vg);nvgIntersectScissor(vg,r.x,r.y,r.w,r.h);
        row_text_scroll.Draw(vg,true,r.x,r.y+r.h/2,r.w,size,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,fg,text);
        nvgRestore(vg);
    };
    const auto path_line=[&](const std::string& text){
        nvgSave(vg);nvgIntersectScissor(vg,54,142,1172,28);
        path_text_scroll.Draw(vg,true,54,156,1172,16,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,muted,text);
        nvgRestore(vg);
    };
    // Work pages share a quiet surface; Home keeps room for its main choices.
    if(page!=Page::Home)violet::Card(vg,{42,106,1196,533},false);
    if(page==Page::Home){
        Text(vg,54,128,28,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,"Manage your games");
        Text(vg,54,170,18,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,"Install, move and organise in one place.");
        const char* names[]={"Installed games","Install games","File manager","Cleanup","SMB / NFS","More"};
        const char* desc[]={"Move, copy or delete games.\nKeep your saved progress.","Install from SD, USB\nor your network.","Browse and organise files\nacross your drives.","Review unused game files\nand unfinished installs.","Find your NAS or PC.\nConnect once, browse anytime.","Saves, apps, game mods\nand helpful tools."};
        const view::MenuIcon symbols[]={view::MenuIcon::HomeLibrary,view::MenuIcon::HomeInstall,view::MenuIcon::FileManager,view::MenuIcon::Cleanup,view::MenuIcon::MorePc,view::MenuIcon::More};
        static_assert(std::size(names)==view::HomeCount && std::size(desc)==view::HomeCount && std::size(symbols)==view::HomeCount);
        for(size_t i=0;i<view::HomeCount;++i){
            const auto r=view::HomeCard(i);const float x=r.x,y=r.y;const bool focus=i==selected;
            violet::Card(vg,{x,y,r.w,r.h},focus);
            MenuIcon(vg,symbols[i],x+22,y+20,42,i%2?violet::Lilac():violet::Accent());
            BoundedLine(vg,{x+80,y+20,r.w-102,42},24,fg,names[i]);
            nexusdialog::Body(vg,{x+22,y+86,r.w-44,64},desc[i],18);
        }
    }else if(page==Page::More){
        Text(vg,64,122,28,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,"More");
        const char* names[]={"PC transfer","Cartridges","Homebrew apps","Save backups","Game mods","Help & recovery"};
        const char* desc[]={"Send files between your\nPC and Switch.","View a cartridge or\nsave a copy.","Open apps and create\nHOME shortcuts.","Back up or restore\nyour game saves.","Manage mods for\nyour games.","Repair tools, guides\nand app updates."};
        const view::MenuIcon symbols[]={view::MenuIcon::MorePc,view::MenuIcon::MoreCartridge,view::MenuIcon::Homebrew,view::MenuIcon::MoreSave,view::MenuIcon::Mods,view::MenuIcon::MoreHelp};
        static_assert(std::size(names)==view::MoreCount && std::size(desc)==view::MoreCount && std::size(symbols)==view::MoreCount);
        for(size_t i=0;i<view::MoreCount;++i){
            const auto r=view::MoreCard(i);const bool focus=i==selected;
            violet::Card(vg,{r.x,r.y,r.w,r.h},focus);
            MenuIcon(vg,symbols[i],r.x+24,r.y+24,44,i%2?violet::Lilac():violet::Accent());
            BoundedLine(vg,{r.x+84,r.y+24,r.w-108,44},23,fg,names[i]);
            nexusdialog::Body(vg,{r.x+24,r.y+94,r.w-48,64},desc[i],19);
        }
    }else if(page==Page::Mods || page==Page::ModFolders){
        const bool choosing=page==Page::ModFolders;
        Text(vg,54,111,26,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,choosing?"Choose an Atmosphere folder":"Game mods on USB");
        path_line(choosing?path:mod_folder);
        const size_t start=selected/7*7;
        for(size_t i=start;i<std::min(Count(),start+7);++i){const float y=174+(i-start)*60;const Vec4 row{54,y,1172,56};
            violet::Card(vg,row,i==selected,true);
            const auto name=choosing?files[i].name:(mods[i].ips?"IPS / ":"Game / ")+mods[i].name;
            const auto state=choosing?std::string("Open"):std::string(mods[i].registered?"Registered":"New")+" / "+(mods[i].ips?"IPS":mods[i].romfs?(mods[i].exefs?"RomFS + ExeFS":"RomFS"):"ExeFS");
            if(choosing)MenuIcon(vg,view::MenuIcon::Folder,70,y+8,40,accent);
            const float text_x=choosing?120:72;
            name_line({text_x,y,832-text_x,56},20,name,i==selected);
            BoundedLine(vg,{856,y,352,56},17,accent,state,true);
        }
        if(!Count())Text(vg,640,320,23,NVG_ALIGN_CENTER,fg,choosing?"No subfolders. X selects this folder.":"No supported mods here. Y: choose another Atmosphere folder.");
        BoundedLine(vg,{54,602,1172,31},16,muted,choosing?"Enter the folder containing contents or exefs_patches, then press X.":"Review before applying. Game data stays on USB; boot files and system modules are excluded.");
    }else if(page==Page::Installed){
        Text(vg,54,116,28,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,"Installed games");
        const auto use_usb=std::string(gfx::getButton(Button::R))+"  Repair USB games";
        nexusdialog::Button(vg,{850,110,376,46},use_usb.c_str(),false);
        if(games.empty()){
            Text(vg,640,296,28,NVG_ALIGN_CENTER,fg,"Your game library is empty");
            Text(vg,640,340,20,NVG_ALIGN_CENTER,muted,"Already have games installed on USB? Choose Repair USB games.");
            Text(vg,640,376,18,NVG_ALIGN_CENTER,muted,"For NSP, NSZ, XCI or XCZ files, go back to Install games.");return;
        }
        const auto start=view::PageStart(selected),end=std::min(games.size(),start+view::PageSize);
        const auto page_label="Page "+std::to_string(selected/view::PageSize+1)+" / "+std::to_string((games.size()+view::PageSize-1)/view::PageSize);
        Text(vg,802,123,18,NVG_ALIGN_RIGHT|NVG_ALIGN_TOP,muted,page_label.c_str());
        for(size_t i=start;i<end;++i){
            const auto& game=games[i];const auto r=view::Card(i);
            const Vec4 card{r.x,r.y,r.w,r.h};
            violet::Card(vg,card,i==selected);
            const auto it=icons.find(game.CacheKey());
            gfx::drawImage(vg,r.x+8,r.y+7,52,52,it==icons.end()?App::GetDefaultImage():it->second,8);
            name_line({r.x+76,r.y+7,r.w-98,28},22,game.name,i==selected);
            const auto location=StorageLabel(game);
            BoundedLine(vg,{r.x+76,r.y+36,r.w-98,24},16,i==selected?accent:muted,location);
        }
        const auto& game=games[selected];const auto detail=view::DetailPanel();
        violet::Card(vg,{detail.x,detail.y,detail.w,detail.h},false,true);
        const auto icon=icons.find(game.CacheKey());
        gfx::drawImage(vg,detail.x+(detail.w-128)/2,detail.y+12,128,128,icon==icons.end()?App::GetDefaultImage():icon->second,12);
        nexusdialog::Body(vg,{detail.x+20,detail.y+152,detail.w-40,56},game.name.c_str(),22);
        char id[32]{};std::snprintf(id,sizeof(id),"%016lX",game.id);
        const auto info_line=[&](float y,const std::string& value){BoundedLine(vg,{detail.x+20,detail.y+y,detail.w-40,22},17,fg,value);};
        info_line(216,"Title ID: "+std::string(id));
        info_line(244,"Version: "+(game.display_version.empty()?std::string("Not available"):game.display_version));
        const auto version=game.has_update?"Update: v"+std::to_string(game.update_version):game.has_base?"Base: v"+std::to_string(game.base_version):std::string("Version record unavailable");
        info_line(270,version+"  |  DLC: "+std::to_string(game.dlc_count));
        u64 total{};bool known=false,all=true;
        for(size_t i=0;i<3;++i){total+=game.installed_bytes[i];known|=game.size_known[i];all&=game.size_known[i];}
        info_line(298,(all?"Installed size: ":"Known size: ")+(known?utils::formatSizeStorage(total):std::string("Not available")));
        const char* storage_names[]={"SD card","Internal","USB drive"};
        for(size_t i=0;i<3;++i)info_line(330+i*26,std::string(storage_names[i])+": "+
            (game.size_known[i]?(game.installed_bytes[i]?utils::formatSizeStorage(game.installed_bytes[i]):std::string("Not installed")):std::string("Unavailable")));
        const auto pages=std::string(gfx::getButton(Button::LEFT))+" "+gfx::getButton(Button::RIGHT)+"  Change page";
        BoundedLine(vg,{54,602,1172,31},16,muted,pages);
    }else if(page==Page::Queue || page==Page::Results){
        const bool results=page==Page::Results;
        const char* heading=results?(batch_result && batch_result->result?"Batch stopped":"Batch complete"):"Install queue";
        Text(vg,54,111,26,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,heading);
        std::string subtitle;
        if(results && batch_result){
            subtitle=std::to_string(batch_result->Count(nexus::batch::State::Installed))+" installed / "+
                std::to_string(batch_result->Count(nexus::batch::State::Failed))+" failed / "+
                std::to_string(batch_result->Count(nexus::batch::State::Cancelled))+" cancelled / "+
                std::to_string(batch_result->Count(nexus::batch::State::Pending))+" not attempted   |   "+batch_source+" to "+nexus::TargetName(batch_result->target);
        }else subtitle="Selection order is install order. Use L/R to arrange base, update, then DLC.";
        BoundedLine(vg,{54,142,1172,28},16,muted,subtitle);
        const size_t start=selected/7*7;
        for(size_t i=start;i<std::min(Count(),start+7);++i){
            const auto& e=results?batch_result->items[i].entry:packages.Entries()[i];
            const float y=174+(i-start)*60;const Vec4 row{54,y,1172,56};
            violet::Card(vg,row,i==selected,true);
            BoundedLine(vg,{72,y,44,56},18,accent,std::to_string(i+1));
            name_line({128,y,736,56},20,e.name,i==selected);
            const auto status=results?(batch_result->items[i].state==nexus::batch::State::Installed &&
                batch_result->items[i].detail.find("icon refresh pending")!=std::string::npos?
                std::string("Installed - icon refresh pending"):std::string(nexus::batch::StateName(batch_result->items[i].state))):utils::formatSizeStorage(e.size);
            auto color=muted;
            if(results){const auto state=batch_result->items[i].state;color=state==nexus::batch::State::Installed?accent:state==nexus::batch::State::Failed?nvgRGB(255,147,147):muted;}
            BoundedLine(vg,{888,y,320,56},17,color,status,true);
        }
        if(!Count())Text(vg,640,320,25,NVG_ALIGN_CENTER,fg,"No packages selected");
        const auto detail=results && batch_result?"Stage: "+batch_result->stage+"  /  A: file result  /  Saved: switch/nexusmanager/batch-last.log":
            "Source files: "+utils::formatSizeStorage(packages.Bytes())+" (not installed size)  /  +: choose SD, Internal Storage or USB";
        BoundedLine(vg,{54,602,1172,31},16,muted,detail);
    }else{
        const bool browsing=page==Page::Files || page==Page::Homebrew;
        const auto heading=page==Page::Cleanup?std::string("Cleanup - ")+(cleanup_tickets?"Unused tickets":nexus::TargetName(cleanup_target)):page==Page::Homebrew?std::string(usb_source?"Homebrew apps on USB":"SD apps for startup timing"):std::string(usb_source?"Install from USB":"Install from SD");
        Text(vg,54,111,26,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,fg,heading.c_str());
        path_line(page==Page::Cleanup?"Select an item to review. Saves and system files are kept.":path);
        if(!Count()){Text(vg,640,320,25,NVG_ALIGN_CENTER,fg,page==Page::Cleanup?"Nothing to clean up":"This folder is empty. Press B to go back.");return;}
        const size_t start=selected/7*7;
        for(size_t i=start;i<std::min(Count(),start+7);++i){
            const float y=174+(i-start)*60;const Vec4 row{54,y,1172,56};
            violet::Card(vg,row,i==selected,true);
            const bool folder=browsing && files[i].directory;
            const std::string name=browsing?files[i].name:cleanup_items[i].name;
            const u64 size=browsing?files[i].size:cleanup_items[i].bytes;
            const bool selectable=page==Page::Files && !files[i].directory && nexus::batch::ValidEntry({files[i].name,files[i].path,files[i].size});
            if(selectable){
                const bool marked=packages.Contains(files[i].path);
                gfx::drawRect(vg,72,y+14,30,28,marked?violet::Accent():violet::Selected(),6);
                if(marked)Text(vg,87,y+20,13,NVG_ALIGN_CENTER|NVG_ALIGN_TOP,accent,std::to_string(packages.Index(files[i].path)+1).c_str());
            }
            if(folder)MenuIcon(vg,view::MenuIcon::Folder,70,y+8,40,accent);
            const float text_x=browsing?120:72;
            name_line({text_x,y,976-text_x,56},21,name,i==selected);
            const auto label=folder?std::string("Open folder"):page==Page::Homebrew && files[i].path.empty()?std::string("Built-in"):
                page==Page::Cleanup && cleanup_tickets?std::string("Review ticket"):
                browsing && usb_source && !files[i].directory && nexus_vsd::Nro(files[i].path)?std::string(envIsNso()?"NRO / Shortcut":"NRO / Launch"):utils::formatSizeStorage(size);
            BoundedLine(vg,{1000,y,208,56},20,muted,label,true);
        }
        if(page==Page::Cleanup)BoundedLine(vg,{54,602,1172,31},16,muted,cleanup_detail);
        if(page==Page::Files)BoundedLine(vg,{54,602,1172,31},16,muted,"Choose a file, or select several to install together.");
        if(page==Page::Homebrew)BoundedLine(vg,{54,602,1172,31},16,muted,!usb_source?"Choose an app to create a timed SD HOME shortcut. Use the same app version and test steps as USB.":envIsNso()?"Choose an app to create a HOME shortcut. Its files stay on USB; keep the drive connected.":"Choose an app to open it or create a HOME shortcut. Keep its data files together on USB.");
    }
}
}
