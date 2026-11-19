// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/nexus_violet.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/release.hpp"
#include "nexus/startup_trace.hpp"
#include "nexus/lifecycle.hpp"
#include "utils/thread.hpp"
#include <algorithm>
#include <atomic>
#include <memory>

namespace sphaira::ui::menu {
namespace {
class StoragePoller {
    Mutex mutex{};UEvent wake{};Thread thread{};
    std::atomic_bool stop{},paused{};bool started{};
    PolledData data{};
    static void Run(void* ptr){static_cast<StoragePoller*>(ptr)->Loop();}
    void Loop(){
        nexus::startup::Event("storage-worker-entered");
        while(!stop){
            if(!paused && nexus::lifecycle::CanPoll()){
                PolledData next{};
                next.sd_valid=R_SUCCEEDED(nexus::startup::Call("widget-sd-size", [&]{ return App::GetSdSize(&next.sd_free,&next.sd_total); })) &&
                    next.sd_total>0 && next.sd_free>=0 && next.sd_free<=next.sd_total;
                if(stop || paused || !nexus::lifecycle::CanPoll())continue;
                next.emmc_valid=R_SUCCEEDED(nexus::startup::Call("widget-internal-size", [&]{ return App::GetEmmcSize(&next.emmc_free,&next.emmc_total); })) &&
                    next.emmc_total>0 && next.emmc_free>=0 && next.emmc_free<=next.emmc_total;
                if(stop || paused || !nexus::lifecycle::CanPoll())continue;
                nexus::Client c;nexus::Status status{};
                const auto rc=c.Query(status);
                next.usb_state=R_FAILED(rc)?(rc==nexus::Unavailable?3:4):2;
                if(R_SUCCEEDED(rc) && status.Available() && !stop && !paused && nexus::lifecycle::CanPoll()){
                    nexus::Space space{};
                    const auto sr=c.GetSpace(status.generation,space);
                    if(R_SUCCEEDED(sr) && space.total && space.free<=space.total){
                        next.usb_free=space.free;next.usb_total=space.total;next.usb_state=1;
                    }else next.usb_state=4;
                }else if(R_SUCCEEDED(rc) && (status.mounted || status.result))next.usb_state=status.result?4:0;
                mutexLock(&mutex);data=next;mutexUnlock(&mutex);
            }
            waitSingle(waiterForUEvent(&wake),2'000'000'000ULL);
        }
    }
public:
    StoragePoller(){
        ueventCreate(&wake,true);
        if(R_SUCCEEDED(utils::CreateThread(&thread,Run,this))){
            if(R_SUCCEEDED(nexus::startup::Call("storage-worker-start", [&]{ return threadStart(&thread); })))started=true;else threadClose(&thread);
        }
    }
    ~StoragePoller(){stop=true;ueventSignal(&wake);if(started){nexus::startup::Call("storage-worker-join", [&]{ return threadWaitForExit(&thread); });threadClose(&thread);}}
    PolledData Get(){mutexLock(&mutex);auto copy=data;mutexUnlock(&mutex);return copy;}
    void Pause(bool value){paused=value;if(!value)ueventSignal(&wake);}
    void Refresh(){ueventSignal(&wake);}
};
std::unique_ptr<StoragePoller> poller;
unsigned users{};
int nexus_logo{};
void StorageCard(NVGcontext* vg,float x,const char* name,bool valid,u64 free,u64 total,const char* unavailable,NVGcolor accent){
    const auto muted=nvgRGB(255,255,255),fg=nvgRGB(255,255,255);

    gfx::drawText(vg,x+14,25,15,accent,name);
    char value[64]{};
    if(valid)snprintf(value,sizeof(value),"%.1f GB free",double(free)/(1024*1024*1024));
    else snprintf(value,sizeof(value),"%s",unavailable);
    // Keep long connection states inside the same text column as capacity.
    nvgSave(vg);
    nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    float font=17,bounds[4]{};
    do {
        nvgFontSize(vg,font);nvgTextBounds(vg,0,0,value,nullptr,bounds);
        if(bounds[2]-bounds[0]<=170 || font<=14)break;
        --font;
    }while(true);
    nvgIntersectScissor(vg,x+14,47,170,23);
    gfx::drawText(vg,x+14,47,font,valid?fg:muted,value);
    nvgRestore(vg);
    gfx::drawRect(vg,x+14,74,170,4,violet::Line(),2);
    if(valid && total){
        const auto used=std::clamp(1.0-double(free)/double(total),0.0,1.0);
        if(used>0)gfx::drawRect(vg,x+14,74,static_cast<float>(170*used),4,accent,2);
    }
}
}
auto MenuBase::GetPolledData(bool refresh)->PolledData {
    if(!poller)return {};
    if(refresh)poller->Refresh();
    return poller->Get(); // no filesystem/USB IPC on the draw thread
}
MenuBase::MenuBase(const std::string& title,u32 flags):m_title(title),m_flags(flags){
    if(!users++){
        poller=std::make_unique<StoragePoller>();
        if(R_SUCCEEDED(romfsInit())){
            nexus_logo=nvgCreateImage(App::GetVg(),"romfs:/theme/nexus-logo.jpg",0);
            romfsExit();
        }
    }
    SetPos(30,100,1220,540);SetAction(Button::START,Action{App::Exit});
}
MenuBase::~MenuBase(){if(!--users){poller.reset();if(nexus_logo>0)nvgDeleteImage(App::GetVg(),nexus_logo);nexus_logo=0;}}
void MenuBase::OnFocusGained(){Widget::OnFocusGained();if(poller)poller->Pause(false);}
void MenuBase::OnFocusLost(){Widget::OnFocusLost();if(poller)poller->Pause(true);}
void MenuBase::Update(Controller* controller,TouchInfo* touch){Widget::Update(controller,touch);}
void MenuBase::Draw(NVGcontext* vg,Theme* theme){
    gfx::drawRect(vg,0,0,1280,720,nvgLinearGradient(vg,0,0,1280,720,nvgRGB(6,12,26),nvgRGB(16,17,36)));
    gfx::drawRect(vg,0,0,1280,100,nvgRGBA(9,16,31,244));
    gfx::drawRect(vg,54,98,1172,1,violet::Line());
    if(nexus_logo>0)gfx::drawImage(vg,54,24,48,48,nexus_logo,8);
    gfx::drawText(vg,118,26,26,nvgRGB(255,255,255),"NEXUS MANAGER");
    const std::string build_version=APP_DISPLAY_VERSION;
    const std::string edition=std::string("v")+build_version.substr(0,build_version.find(' '))+
        (App::IsApplet()?"  /  Limited memory mode":"");
    gfx::drawText(vg,119,65,15,nvgRGB(255,255,255),edition.c_str());
    const auto p=GetPolledData();
    StorageCard(vg,600,"SD card",p.sd_valid,p.sd_free,p.sd_total,"Unavailable",violet::Accent());
    StorageCard(vg,810,"Console",p.emmc_valid,p.emmc_free,p.emmc_total,"Unavailable",violet::Lilac());
    constexpr const char* usb_states[]={"Checking...","Connected","Not connected","Firmware unavailable","Storage error"};
    StorageCard(vg,1020,"USB",p.usb_state==1,p.usb_free,p.usb_total,usb_states[std::min(p.usb_state,4u)],violet::Accent());
    gfx::drawRect(vg,0,653,1280,67,nvgRGBA(9,16,31,244));
    gfx::drawRect(vg,54,653,1172,1,violet::Line());
    Widget::Draw(vg,theme);
    m_scroll_sub_heading.Draw(vg,true,50,673,205,15,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,violet::Lilac(),m_sub_heading);
}
} // namespace sphaira::ui::menu
