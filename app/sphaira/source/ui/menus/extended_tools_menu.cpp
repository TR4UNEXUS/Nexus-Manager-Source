// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/extended_tools_menu.hpp"
#include "ui/menus/manager_tools_menu.hpp"
#include "ui/nexus_violet.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "nexus/cartridge.hpp"
#include "nexus/transfer.hpp"
#include "nexus/transfer_exports.hpp"
#include "app.hpp"
#include <algorithm>
namespace sphaira::ui::menu::extended {
namespace {
const char* const cards[]={"Cartridge information","Export trimmed XCI","Export full XCI","Browse cartridge exports"};
const char* const transfers[]={"USB cable","Wi-Fi / FTP"};
const char* const transferHelp[]={"Connect directly to your PC and open its file browser (MTP).","Connect on the same network. No login details needed."};
Vec4 Row(Mode mode,size_t i){return mode==Mode::Transfer?Vec4{54,174+i*88.f,1172,76}:Vec4{54,166+i*51.f,1172,46};}
void Text(NVGcontext* vg,float x,float y,float size,NVGcolor colour,const std::string& text){gfx::drawText(vg,x,y,size,colour,text.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_TOP);}
void Browse(const char* p){App::Push<managertools::Menu>(managertools::Mode::Files,p);}
}
Menu::Menu(Mode m):MenuBase("Nexus Manager",MenuFlag_None),mode(m){
    SetAction(Button::A,Action{"Open",[this]{Open();}});SetAction(Button::B,Action{"Back",[this]{Back();}});
    SetAction(Button::UP,Action{[this]{if(selected)--selected;}});SetAction(Button::DOWN,Action{[this]{if(selected+1<Count())++selected;}});
    SetAction(Button::START,Action{"Exit",[this]{nexus::transfer::Stop();App::Exit();}});
}
Menu::~Menu(){if(mode==Mode::Transfer)nexus::transfer::Stop();}
size_t Menu::Count()const{return mode==Mode::Transfer?std::size(transfers):std::size(cards);}
void Menu::Back(){if(mode==Mode::Transfer)nexus::transfer::Stop();SetPop();}
void Menu::Card(bool dump,bool trim){
    auto run=[dump,trim]{auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,dump?"Cartridge export":"Cartridge information","Keep the cartridge inserted",[detail,dump,trim](ProgressBox* p){auto rc=nexusmanager::Cartridge(p,dump,trim,*detail);nexus::Log("manager-cartridge",rc);return rc;},[detail](Result rc){if(R_FAILED(rc))App::PushErrorBox(rc,*detail);else App::Push<OptionBox>(*detail,"Close",[](auto){});},ProgressBoxOption::None,dump?nexus::safety::Operation::MayWrite:nexus::safety::Operation::ReadOnly);
    };
    if(!dump){run();return;}
    App::Push<OptionBox>("Export your own cartridge to SD?\n\nClose the game first and leave the cartridge inserted. The original is unchanged. The private card certificate is removed. Copy and full readback verification can take time.\n\nIf HOME no longer shows the cartridge afterwards, remove and reinsert it. Cancellation keeps an unfinished .partial file.","Cancel","Export",0,[run](auto choice){if(choice && *choice==1)run();});
}
void Menu::Open(){
    if(App::IsApplet()){App::Push<OptionBox>("Use the standalone HOME app or full-memory NRO mode (hold R while starting a game). These tools are disabled in Album mode.","Close",[](auto){});return;}
    if(mode==Mode::Cartridge){if(selected==3)Browse("/switch/nexusmanager/transfers/exports");else Card(selected!=0,selected!=2);return;}
    if(nexus::transfer::Active()!=nexus::transfer::Mode::Off){App::Push<OptionBox>("Your connection is already running.\n\nFinish transferring on your PC, then press B to disconnect and return. Open PC transfer again to choose a different connection.","Close",[](auto){});return;}
    const auto cable=selected==0;
    std::string help="Sphaira PC transfer\n\nGames: open a game folder and copy its NSP files. Base game, updates and DLC are separate exports.\nSD / USB Install: upload NSP, NSZ, XCI or XCZ. Allow space for upload and installation.\nSave uploads still need Switch confirmation to restore.";
    help+=cable?"\n\nmicroSD card is the full SD browser: PC edits and deletes change real files. Back up important files. Use a data cable directly to the PC. For docked USB storage, use FTP.":"\n\nFTP: sdmc:/ is your full SD; games:/ has game exports. Upload to usb-install:/ for docked USB, or sd-install:/ for SD. Anonymous and unencrypted: use a trusted private network only. PC edits/deletes under sdmc:/ change real files.";
    App::Push<OptionBox>(help,"Cancel","Connect",0,[cable](auto answer){
        if(!answer || *answer!=1)return;
        App::Push<ProgressBox>(0,"Preparing PC destinations","SD, USB, game exports and save backups",[](ProgressBox* p){return nexus::transfer::PrepareSession(p);},[cable](Result rc){
            if(R_SUCCEEDED(rc))rc=nexus::transfer::Start(cable?nexus::transfer::Mode::Cable:nexus::transfer::Mode::Ftp);
            if(R_FAILED(rc)){nexus::Log("pc-connect-failed",rc);nexus::transfer::Stop();App::PushErrorBox(rc,nexus::transfer::StartupMessage());}
        });
    });
}
void Menu::Update(Controller* c,TouchInfo* touch){MenuBase::Update(c,touch);if(HasFocus()&&touch->is_clicked)for(size_t i=0;i<Count();++i)if(touch->in_range(Row(mode,i))){if(selected==i)Open();else selected=i;break;}if(mode==Mode::Transfer && HasFocus())nexus::transfer::ShowNextInstall();}
void Menu::Draw(NVGcontext* vg,Theme* theme){
    MenuBase::Draw(vg,theme);violet::Card(vg,{42,106,1196,533},false);
    auto fg=theme->GetColour(ThemeEntryID_TEXT);auto muted=violet::Lilac();
    Text(vg,62,116,27,fg,mode==Mode::Cartridge?"Cartridges":"PC transfer");
    auto rows=mode==Mode::Cartridge?cards:transfers;
    for(size_t i=0;i<Count();++i){const auto row=Row(mode,i);violet::Card(vg,row,selected==i,true);Text(vg,76,row.y+12,22,fg,rows[i]);if(mode==Mode::Transfer)Text(vg,76,row.y+44,16,muted,transferHelp[i]);}
    if(mode==Mode::Transfer)Text(vg,72,365,20,fg,"Connection");
    std::string text=mode==Mode::Transfer?nexus::transfer::Status():"Your cartridge is read-only. Exports are checked before they finish.\nTo install an export, choose Install games, then SD card.\nExports are saved in transfers/exports and can be sent to your PC.";
    float y=mode==Mode::Transfer?405:390;size_t start=0;
    do {auto end=text.find('\n',start);Text(vg,72,y,start?16:21,start?muted:fg,text.substr(start,end==text.npos?text.size()-start:end-start));y+=start?25:34;if(end==text.npos)break;start=end+1;}while(y<595);
    if(mode==Mode::Transfer)Text(vg,72,607,14,muted,nexus::transfer::Active()==nexus::transfer::Mode::Off?"Sphaira SD and Games browser. Choose install destinations on your PC.":nexus::transfer::SharedFolder());
}
}
