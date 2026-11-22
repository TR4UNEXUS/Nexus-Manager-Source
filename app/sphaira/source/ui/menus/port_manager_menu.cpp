#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/port_manager_menu.hpp"
#include "ui/nexus_dialog.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "nexus/game_cleanup.hpp"
#include "nexus/port_transfer.hpp"
#include "nexus/homebrew.hpp"
#include "nexus/vsd_policy.hpp"
#include "app.hpp"
#include "utils/utils.hpp"
#include "utils/thread.hpp"
#include "image.hpp"
#include "title_info.hpp"
#include "ui/port_library_layout.hpp"
namespace sphaira::ui::menu::portmanager {
namespace {
namespace f=nexus::files;
void Line(NVGcontext* vg,float x,float y,float w,float size,const std::string& text){nvgSave(vg);nvgScissor(vg,x,y,w,size+8);gfx::drawText(vg,x,y,size,nexusdialog::Text(),text.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgRestore(vg);}
void Show(Result rc,const std::string& detail){if(R_FAILED(rc))App::PushErrorBox(rc,detail);else App::Push<OptionBox>(detail,"Close"_nx,[](auto){});}
enum class Pick {Nro,Folder,Mod};
struct Picker final:MenuBase {
    Pick kind;bool usb,change_storage,pending{true};u64 generation{};size_t selected{};std::string path="/";std::vector<nexusapp::Item> rows;std::function<void(nexusfiles::Selection)> callback;
    Picker(Pick k,bool u,bool storage,std::function<void(nexusfiles::Selection)> cb):MenuBase(k==Pick::Nro?"Choose a port NRO"_nx:k==Pick::Folder?"Link a port folder"_nx:"Import a mod"_nx,MenuFlag_None),kind(k),usb(u),change_storage(storage),callback(std::move(cb)){
        SetAction(Button::A,Action{"Open / select"_nx,[this]{Open();}});SetAction(Button::B,Action{"Back"_nx,[this]{if(path=="/")SetPop();else{path=f::Parent(path);Refresh();}}});
        if(kind!=Pick::Nro)SetAction(Button::X,Action{"Use this folder"_nx,[this]{if(kind!=Pick::Nro&&path!="/")Choose({f::Leaf(path),path,0,true});}});
        if(change_storage)SetAction(Button::R,Action{"SD / USB",[this]{usb=!usb;path="/";Refresh();}});
        SetAction(Button::UP,Action{[this]{if(selected)--selected;}});SetAction(Button::DOWN,Action{[this]{if(selected+1<rows.size())++selected;}});
        SetAction(Button::LEFT,Action{[this]{selected=selected>=7?selected-7:0;}});SetAction(Button::RIGHT,Action{[this]{if(!rows.empty())selected=std::min(selected+7,rows.size()-1);}});
    }
    const char* GetShortTitle()const override{return "Nexus Manager";}
    void Choose(const nexusapp::Item& item){auto cb=callback;SetPop();cb({usb,generation,item});}
    void Refresh(){rows.clear();selected=0;auto data=std::make_shared<std::vector<nexusapp::Item>>();auto gen=std::make_shared<u64>();const auto u=usb;const auto folder=path;
        App::Push<ProgressBox>(0,"Reading folder"_nx,folder,[u,folder,data,gen](ProgressBox* p){return nexusfiles::ListFiles(p,u,folder,*data,*gen);},[this,data,gen](Result rc){if(R_FAILED(rc)){Show(rc,"Could not read this storage. Nothing was changed."_nx);return;}generation=*gen;rows=std::move(*data);std::erase_if(rows,[&](const auto& item){return !item.directory&&(kind==Pick::Folder||(kind==Pick::Nro?!nexus_vsd::Nro(item.path):!f::Fold(item.path).ends_with(".zip")));});},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);}
    void Open(){if(selected>=rows.size())return;const auto item=rows[selected];if(item.directory){path=item.path;Refresh();}else Choose(item);}
    void Update(Controller* c,TouchInfo* t)override{MenuBase::Update(c,t);if(pending&&HasFocus()){pending=false;Refresh();}if(HasFocus()&&t->is_clicked){const auto start=selected/7*7;for(size_t n=start;n<std::min(rows.size(),start+7);++n)if(t->in_range({54,180+(n-start)*58.f,1172,54})){if(selected==n)Open();else selected=n;break;}}SetSubHeading(std::to_string(rows.empty()?0:selected+1)+" / "+std::to_string(rows.size()));}
    void Draw(NVGcontext* vg,Theme* theme)override{MenuBase::Draw(vg,theme);violet::Card(vg,{42,106,1196,533},false);Line(vg,62,118,1156,25,GetTitle());Line(vg,62,151,1156,16,std::string(usb?"USB:":"SD:")+path);const auto start=selected/7*7;
        for(size_t n=start;n<std::min(rows.size(),start+7);++n){const auto y=180+(n-start)*58.f;violet::Card(vg,{54,y,1172,54},n==selected,true);Line(vg,74,y+15,970,20,rows[n].name);Line(vg,1070,y+17,136,16,rows[n].directory?"Folder"_nx:"Select"_nx);}
        if(rows.empty())Line(vg,72,210,1120,20,"No matching items here."_nx);Line(vg,62,605,1150,16,kind==Pick::Nro?"Select an NRO to manage its port folder and create a HOME shortcut."_nx:kind==Pick::Folder?"Open the port's exact folder, then press X to link it."_nx:"A selects a ZIP. Open a mod folder and press X to import its contents."_nx);}
};
}
Menu::Menu(std::optional<nexusports::Port> initial):MenuBase("Ports & homebrew"_nx,MenuFlag_None),port(std::move(initial)){if(port)usb=port->usb;title_ready=R_SUCCEEDED(title::Init());Setup();
    SetAction(Button::UP,Action{[this]{selected=nexus::portgrid::Navigate(selected,Count(),port?-1:-6);}});SetAction(Button::DOWN,Action{[this]{selected=nexus::portgrid::Navigate(selected,Count(),port?1:6);}});SetAction(Button::LEFT,Action{[this]{selected=nexus::portgrid::Navigate(selected,Count(),port?-7:-1);}});SetAction(Button::RIGHT,Action{[this]{selected=nexus::portgrid::Navigate(selected,Count(),port?7:1);}});}
namespace {
std::string IconKey(const nexusports::Port& p){return std::string(p.usb?"usb:":"sd:")+std::to_string(p.generation)+":"+p.nro+":"+std::to_string(p.application);}
int MakeIcon(const std::vector<u8>& bytes){auto image=ImageLoadIcon(bytes);if(image.data.empty())return 0;
    if(image.w!=256||image.h!=256)image=ImageResize(image.data,image.w,image.h,256,256);
    return image.data.empty()?0:nvgCreateImageRGBA(App::GetVg(),image.w,image.h,0,image.data.data());}
}
Menu::~Menu(){ClearIcons();if(title_ready)title::Exit();}
void Menu::OnFocusLost(){StopIcon();MenuBase::OnFocusLost();}
void Menu::ReadIconWorker(void* arg){auto& job=*static_cast<IconJob*>(arg);job.result=nexusports::ReadIcon(job.port,job.bytes);job.done.store(true);}
void Menu::DropIcon(const std::string& key){
    const auto it=icons.find(key);if(it==icons.end())return;
    if(it->second.image>0)nvgDeleteImage(App::GetVg(),it->second.image);
    icons.erase(it);
}
int Menu::CachedImage(const std::string& key){
    const auto it=icons.find(key);if(it==icons.end())return 0;
    it->second.used=++icon_clock;return it->second.image;
}
void Menu::StoreIcon(const std::string& key,int image){
    DropIcon(key);
    if(icons.size()>=IconCacheLimit){
        const auto oldest=std::min_element(icons.begin(),icons.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        DropIcon(oldest->first);
    }
    // Remember missing/invalid icons too, avoiding repeated drive reads.
    icons.emplace(key,CachedIcon{image,++icon_clock});
}
void Menu::StopIcon(bool keep){
    if(icon_thread_started){threadWaitForExit(&icon_thread);threadClose(&icon_thread);icon_thread_started=false;}
    if(icon_job){
        if(keep&&icon_job->done.load())StoreIcon(icon_job->key,R_SUCCEEDED(icon_job->result)?MakeIcon(icon_job->bytes):0);
        icon_job.reset();
    }
}
void Menu::ClearIcons(){
    StopIcon(false);
    for(const auto& [key,icon]:icons)if(icon.image>0)nvgDeleteImage(App::GetVg(),icon.image);
    icons.clear();icon_clock=0;title_requested.clear();
}
void Menu::PollIcons(){
    const auto start=nexus::portgrid::Start(selected),end=std::min(ports.size(),start+nexus::portgrid::PageSize);
    // Protect this page's existing images before admitting a finished job.
    for(size_t n=start;n<end;++n)CachedImage(IconKey(ports[n]));
    if(icon_job&&icon_job->done.load())StopIcon();
    for(size_t n=start;n<end;++n){
        const auto& item=ports[n];const auto key=IconKey(item);const auto cached=icons.find(key);
        if(cached!=icons.end()&&(cached->second.image>0||cached->second.title_checked||!title_ready||!item.application))continue;
        bool title_finished=!title_ready||!item.application;
        if(title_ready&&item.application){
            if(title_requested.insert(item.application).second)title::PushAsync(item.application);
            const auto* title=title::GetAsync(item.application);
            title_finished=title&&title->status==title::NacpLoadStatus::Loaded;
            if(title_finished&&cached!=icons.end())cached->second.title_checked=true;
            if(title_finished&&!title->icon.empty()){
                const auto image=MakeIcon(title->icon);if(image>0){StoreIcon(key,image);break;}
            }
        }
        // A missing NRO icon can still be replaced by a HOME icon that finishes
        // loading later. Do not retry the NRO read while waiting for it.
        if(cached!=icons.end())continue;
        if(item.nro.empty()){if(title_finished)StoreIcon(key,0);continue;}
        if(icon_job)continue;
        icon_job=std::make_unique<IconJob>();icon_job->key=key;icon_job->port=item;
        const auto rc=utils::CreateThread(&icon_thread,ReadIconWorker,icon_job.get(),64*1024);
        if(R_FAILED(rc)){StoreIcon(key,0);icon_job.reset();continue;}
        if(R_FAILED(threadStart(&icon_thread))){threadClose(&icon_thread);StoreIcon(key,0);icon_job.reset();continue;}
        icon_thread_started=true;break;
    }
}
size_t Menu::Count()const{return port?(mods_page?mods.size():(port->nro.empty()?4:6)):ports.size();}
void Menu::Setup(){for(auto b:{Button::X,Button::Y,Button::R,Button::R2,Button::SELECT})RemoveAction(b);SetAction(Button::A,Action{"Manage"_nx,[this]{Open();}});SetAction(Button::B,Action{"Back"_nx,[this]{Back();}});SetAction(Button::START,Action{"Exit"_nx,App::Exit});SetAction(Button::Y,Action{"Refresh"_nx,[this]{ClearIcons();Refresh();}});
    if(!port){SetAction(Button::X,Action{"Add NRO"_nx,[this]{AddNro();}});SetAction(Button::R,Action{"Filter"_nx,[this]{Storage();}});}else if(mods_page){SetAction(Button::X,Action{"Import mod"_nx,[this]{Import();}});SetAction(Button::R2,Action{"Delete mod"_nx,[this]{DeleteMod();}});}}
void Menu::Back(){selected=0;if(mods_page){mods_page=false;Setup();}else if(port){port.reset();Refresh();Setup();}else SetPop();}
void Menu::Storage(){filter=(filter+1)%3;Refresh();}
void Menu::Refresh(){selected=0;if(App::IsApplet()){status="Use the HOME app or full-memory homebrew mode."_nx;return;}
    auto detail=std::make_shared<std::string>();if(port){if(!mods_page){status="Manage the port's files, mods and HOME shortcut separately."_nx;return;}auto found=std::make_shared<std::vector<nexusports::Mod>>();const auto item=*port;
        App::Push<ProgressBox>(0,"Reading port mods"_nx,item.name,[item,found,detail](ProgressBox* p){return nexusports::ListMods(p,item,*found,*detail);},[this,found,detail](Result rc){mods.clear();status=*detail;if(R_FAILED(rc))Show(rc,*detail);else mods=std::move(*found);},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }else{
        StopIcon();auto found=std::make_shared<std::vector<nexusports::Port>>();const auto selected_filter=filter;
        App::Push<ProgressBox>(0,"Finding ports"_nx,nexus::portgrid::Filter(filter),[selected_filter,found,detail](ProgressBox* p)->Result{
            std::string availability;Result first_error{};bool scanned=false;
            for(bool source:{false,true}){if((selected_filter==1&&source)||(selected_filter==2&&!source))continue;
                R_TRY(p->ShouldExitResult());std::vector<nexusports::Port> next;std::string note;
                const auto rc=nexusports::Scan(p,source,next,note);R_TRY(p->ShouldExitResult());
                if(R_FAILED(rc)){if(!first_error)first_error=rc;availability+=source?"USB unavailable. "_nx:"SD unavailable. "_nx;continue;}
                scanned=true;found->insert(found->end(),std::make_move_iterator(next.begin()),std::make_move_iterator(next.end()));
            }
            std::sort(found->begin(),found->end(),[](const auto& a,const auto& b){const auto x=f::Fold(a.name),y=f::Fold(b.name);return x==y?a.usb<b.usb:x<y;});
            *detail=availability+"Choose a port. R filters All, SD or USB."_nx;return scanned?0:first_error;
        },[this,found,detail](Result rc){ports.clear();status=*detail;if(R_FAILED(rc))Show(rc,"Could not read the selected storage. Reconnect it and refresh."_nx);else ports=std::move(*found);},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    }}

void Menu::Open(){if(App::IsApplet())return;if(!port){if(selected<ports.size()){port=ports[selected];selected=0;status="Create a shortcut, manage mods or review the port's folders."_nx;Setup();}return;}if(mods_page){ModActions();return;}Actions();}
void Menu::Actions(){constexpr size_t data_actions[]={1,2,4,5};switch(port&&port->nro.empty()?data_actions[selected]:selected){case 0:Shortcut();break;case 1:mods_page=true;Refresh();Setup();break;case 2:Folders();break;case 3:MoveCopy();break;case 4:RemoveFiles();break;case 5:RemoveShortcut();break;}}
void Menu::AddNro(){App::Push<Picker>(Pick::Nro,usb,true,[this](nexusfiles::Selection s){auto chosen=std::make_shared<nexusports::Port>();auto detail=std::make_shared<std::string>();nexusapp::Game game;game.name=s.item.name;game.forwarder_path="sdmc:"+s.item.path;game.forwarder_usb_path=s.item.path;game.usb_forwarder=s.usb;
    App::Push<ProgressBox>(0,"Reading port"_nx,s.item.name,[game,chosen,detail](ProgressBox* p){R_TRY(nexusports::FromGame(p,game,*chosen,*detail));return nexusports::RememberPort(p,*chosen);},[this,chosen,detail](Result rc){if(R_FAILED(rc)){Show(rc,*detail);return;}port=*chosen;usb=port->usb;selected=0;status=*detail;Setup();},ProgressBoxOption::None,nexus::safety::Operation::MayWrite);});}
void Menu::Shortcut(){if(!port||port->nro.empty()){Show(0,"This port uses an installed HOME application rather than an NRO. Keep or reinstall its original application; an NRO forwarder cannot be created for it."_nx);return;}const auto chosen=*port;
    App::Push<OptionBox>("Create a HOME shortcut for "_nx+chosen.name+"?\n\n"+std::string(chosen.usb?"USB:":"SD:")+chosen.nro+"\n\nThe launcher goes to internal storage. Port files stay where they are. Keep the drive connected for USB ports."_nx,"Cancel"_nx,"Create"_nx,0,[this,chosen](auto answer){if(!answer||*answer!=1)return;auto next=std::make_shared<nexusports::Port>(chosen);App::Push<ProgressBox>(0,"Creating HOME shortcut"_nx,chosen.name,[next](ProgressBox* p){return nexusports::CreateShortcut(p,*next);},[this,next](Result rc){if(R_SUCCEEDED(rc))port=*next;Show(rc,R_SUCCEEDED(rc)?"HOME shortcut created. Exit Manager and open it from HOME."_nx:"Could not create the shortcut. If a USB shortcut already exists, remove it here before recreating it. Port files were kept."_nx);});});}
void Menu::Folders(){if(!port)return;auto menu=std::make_unique<Sidebar>(port->name,"Linked port folders"_nx,Sidebar::Side::RIGHT,1280.f);for(const auto& root:port->roots)menu->Add<SidebarEntryCallback>(std::string(port->usb?"USB:":"SD:")+root,[this,root]{if(!port||port->roots.size()<2){Show(0,root+"\n\nThis is the port's main folder. It appears in the deletion review and mod destinations."_nx);return;}const auto chosen=*port;App::Push<OptionBox>(root+"\n\nUnlink this folder? Its files will be kept and it will no longer appear in port deletion or mod destinations."_nx,"Keep linked"_nx,"Unlink"_nx,0,[this,chosen,root](auto answer){if(!answer||*answer!=1)return;auto next=std::make_shared<nexusports::Port>(chosen);auto detail=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Unlinking folder"_nx,root,[next,root,detail](ProgressBox* p){return nexusports::UnlinkFolder(p,*next,root,*detail);},[this,next,detail](Result rc){if(R_SUCCEEDED(rc))port=*next;Show(rc,*detail);});});},true);
    menu->Add<SidebarEntryCallback>("Link another folder"_nx,[this]{const auto chosen=*port;App::Push<Picker>(Pick::Folder,chosen.usb,false,[this,chosen](nexusfiles::Selection s){
        App::Push<OptionBox>("Link this folder to "_nx+chosen.name+"?\n\n"+s.item.path+"\n\nOnly link files belonging to this port. The folder will be included when reviewing port deletion."_nx,"Cancel"_nx,"Link folder"_nx,0,[this,chosen,s](auto answer){if(!answer||*answer!=1)return;auto next=std::make_shared<nexusports::Port>(chosen);auto detail=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Linking port folder"_nx,s.item.path,[next,s,detail](ProgressBox* p){return nexusports::LinkFolder(p,*next,s,*detail);},[this,next,detail](Result rc){if(R_SUCCEEDED(rc))port=*next;Show(rc,*detail);});});});},true,"Add a separate data folder, including an application folder under atmosphere/contents."_nx);App::Push(std::move(menu));}
void Menu::MoveCopy(){
    if(!port||port->nro.empty()){Show(0,"This transfer tool moves NRO port folders. Atmosphere content keeps its existing firmware mapping."_nx);return;}
    const auto chosen=*port;auto choices=std::make_unique<Sidebar>("Transfer port"_nx,chosen.name,Sidebar::Side::RIGHT,1280.f);
    for(bool move:{false,true})choices->Add<SidebarEntryCallback>(std::string(move?"Move to "_nx:"Copy to "_nx)+(chosen.usb?"SD card"_nx:"USB drive"_nx),[this,chosen,move]{
        auto plan=std::make_shared<nexus::port::Plan>();auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Review port transfer"_nx,chosen.name,[chosen,move,plan,detail](ProgressBox* p)->Result{
            return nexusports::PrepareTransfer(p,chosen,move,*plan,*detail);
        },[this,chosen,plan,detail](Result rc){if(R_FAILED(rc)){Show(rc,*detail);return;}App::Push<OptionBox>(*detail,"Cancel"_nx,plan->move?"Move port"_nx:"Copy port"_nx,0,[this,chosen,plan](auto answer){if(!answer||*answer!=1)return;auto report=std::make_shared<nexus::port::Report>();auto destination=std::make_shared<nexusports::Port>();App::Push<ProgressBox>(0,"Transferring port"_nx,plan->name,[chosen,plan,report,destination](ProgressBox* p){return nexusports::Transfer(p,chosen,*plan,*report,*destination);},[this,plan,report,destination](Result result){if(R_SUCCEEDED(result)){port=*destination;usb=port->usb;selected=0;Setup();}Show(result,report->Message(plan->move));});});},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);
    },true,"Copies the port and its linked data folders. No forwarder is required; HOME shortcuts are managed separately."_nx);App::Push(std::move(choices));
}
void Menu::RemoveFiles(){
    if(!port)return;const auto chosen=*port;
    std::string prompt="Permanently delete "_nx+chosen.name+" from "_nx+(chosen.usb?"USB":"SD card"_nx)+"?\n\n";
    prompt+="All "_nx+std::to_string(chosen.roots.size())+" linked port folders/files will be removed.\n"_nx;
    for(size_t i=0;i<std::min<size_t>(chosen.roots.size(),3);++i)prompt+=chosen.roots[i]+"\n";
    if(chosen.roots.size()>3)prompt+="And "_nx+std::to_string(chosen.roots.size()-3)+" more shown in Linked folders.\n"_nx;
    prompt+="\nSaves/settings INSIDE these folders will be deleted. Console saves and the other drive are kept. No backup. The HOME shortcut is separate."_nx;
    App::Push<OptionBox>(prompt,"Cancel"_nx,"Delete permanently"_nx,0,[this,chosen](auto answer){
        if(!answer||*answer!=1)return;auto detail=std::make_shared<std::string>();
        App::Push<ProgressBox>(0,"Deleting port"_nx,chosen.name,[chosen,detail](ProgressBox* p)->Result{
            nexusports::Removal plan;R_TRY(nexusports::PrepareRemoval(p,chosen,plan,*detail));return nexusports::Remove(p,plan,*detail);
        },[this,detail](Result rc){status=*detail;Show(rc,*detail);});
    });
}
void Menu::RemoveShortcut(){
    if(!port||!port->application){Show(0,"No HOME shortcut title ID is linked yet. Refresh after creating a shortcut, or select its HOME entry in Installed games and choose Manage port."_nx);return;}
    const auto chosen=*port;auto targets=std::make_unique<Sidebar>("Remove HOME shortcut"_nx,chosen.name+" / Port folders stay unchanged"_nx,Sidebar::Side::RIGHT,1280.f);
    for(const auto target:{nexus::Target::Internal,nexus::Target::Sd,nexus::Target::Usb})
        targets->Add<SidebarEntryCallback>(std::string("Remove launcher from "_nx)+nexus::TargetName(target),[this,chosen,target]{
            App::Push<OptionBox>("Remove the installed HOME launcher for "_nx+chosen.name+" from "_nx+nexus::TargetName(target)+"?\n\nPort folders and saved progress are kept. Any installed update/DLC belonging to this launcher on this storage will also be removed."_nx,"Cancel"_nx,"Remove launcher"_nx,0,[this,chosen,target](auto answer){
                if(!answer||*answer!=1)return;auto report=std::make_shared<nexusapp::GameRemovalReport>();
                App::Push<ProgressBox>(0,"Removing HOME launcher"_nx,"Port folders are kept"_nx,[chosen,target,report](ProgressBox* p)->Result{
                    nexusapp::GameRemovalPlan plan;const auto rc=nexusports::PrepareShortcutRemoval(p,chosen,target,plan);
                    if(R_FAILED(rc)){report->detail=plan.detail;return rc;}return nexusapp::ApplyGameRemoval(p,plan,*report);
                },[this,report](Result result){Show(result,report->detail);});
            });
        },true,target==nexus::Target::Internal?"Nexus forwarders are normally installed here."_nx:"Use this if your original launcher was installed on this storage."_nx);
    App::Push(std::move(targets));
}
void Menu::Import(){if(!port)return;const auto chosen=*port;auto destinations=std::make_unique<Sidebar>("Mod destination"_nx,"Choose which port folder the mod files belong in"_nx,Sidebar::Side::RIGHT,1280.f);
    for(const auto& root:chosen.roots)destinations->Add<SidebarEntryCallback>(root,[this,chosen,root]{App::Push<Picker>(Pick::Mod,chosen.usb,true,[this,chosen,root](nexusfiles::Selection source){
        App::Push<OptionBox>("Import "_nx+source.item.name+"?\n\nDestination when enabled:\n"_nx+std::string(chosen.usb?"USB:":"SD:")+root+"\n\nChoose a package whose contents are relative to this folder. Import stores a checked copy on the port's drive. It does not enable the mod or change the game yet."_nx,"Cancel"_nx,"Import"_nx,0,[this,chosen,root,source](auto answer){if(!answer||*answer!=1)return;auto mod=std::make_shared<nexusports::Mod>();auto detail=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Importing port mod"_nx,source.item.name,[chosen,root,source,mod,detail](ProgressBox* p){return nexusports::ImportMod(p,chosen,root,source,*mod,*detail);},[this,detail](Result rc){pending=true;Show(rc,*detail);});});});},true);
    App::Push(std::move(destinations));}
void Menu::ModActions(){if(!port||selected>=mods.size())return;const auto chosen=*port;const auto mod=mods[selected];
    if(mod.active||mod.pending){App::Push<OptionBox>(mod.name+"\n\nDisable / recover this mod? Original files will be restored. Files edited after activation stop recovery so your changes are not silently discarded. Close the port first."_nx,"Cancel"_nx,"Restore originals"_nx,0,[this,chosen,mod](auto answer){if(!answer||*answer!=1)return;auto detail=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Restoring original port files"_nx,mod.name,[chosen,mod,detail](ProgressBox* p){return nexusports::Disable(p,chosen,mod,*detail);},[this,detail](Result rc){if(port)DropIcon(IconKey(*port));pending=true;Show(rc,*detail);});});return;}
    auto plan=std::make_shared<nexus::ports::ModPlan>();auto detail=std::make_shared<std::string>();App::Push<ProgressBox>(0,"Review port mod"_nx,mod.name,[chosen,mod,plan,detail](ProgressBox* p){return nexusports::ReviewEnable(p,chosen,mod,*plan,*detail);},[this,chosen,plan,detail](Result rc){if(R_FAILED(rc)){Show(rc,*detail);return;}App::Push<OptionBox>(*detail,"Cancel"_nx,"Enable mod"_nx,0,[this,chosen,plan,detail](auto answer){if(!answer||*answer!=1)return;App::Push<ProgressBox>(0,"Enabling port mod"_nx,plan->name,[chosen,plan,detail](ProgressBox* p){return nexusports::Enable(p,chosen,*plan,*detail);},[this,detail](Result result){if(port)DropIcon(IconKey(*port));pending=true;Show(result,*detail);});});},ProgressBoxOption::None,nexus::safety::Operation::ReadOnly);}
void Menu::DeleteMod(){
    if(!port||selected>=mods.size())return;const auto mod=mods[selected];
    if(mod.active||mod.pending){Show(0,"Disable / recover the mod before removing its stored copy. Original-file backups must be restored first."_nx);return;}
    const auto chosen=*port;
    App::Push<OptionBox>("Delete the stored copy of "_nx+mod.name+"?\n\n"+std::string(chosen.usb?"USB:":"SD:")+mod.path+"\n\nThe port's live files are kept."_nx,"Cancel"_nx,"Delete mod"_nx,0,[this,chosen,mod](auto answer){
        if(!answer||*answer!=1)return;auto report=std::make_shared<nexusfiles::Report>();
        App::Push<ProgressBox>(0,"Deleting stored mod"_nx,mod.name,[chosen,mod,report](ProgressBox* p)->Result{
            std::vector<nexusports::Mod> current;R_TRY(nexusports::ListMods(p,chosen,current,report->detail));
            auto row=std::find_if(current.begin(),current.end(),[&](const auto& item){return item.path==mod.path;});
            R_UNLESS(row!=current.end()&&!row->active&&!row->pending,nexus::Invalid);
            nexusfiles::DeletePlan plan;R_TRY(nexusfiles::PrepareDelete(p,{chosen.usb,chosen.generation,{mod.name,mod.path,0,true}},plan,report->detail));
            return nexusfiles::Delete(p,plan,*report);
        },[this,report](Result result){pending=true;Show(result,R_SUCCEEDED(result)?"Stored mod deleted. Port files are unchanged."_nx:report->detail);});
    });
}
void Menu::Update(Controller* c,TouchInfo* t){
    MenuBase::Update(c,t);if(pending&&HasFocus()){pending=false;Refresh();}
    if(HasFocus()&&t->is_clicked){const auto start=port?selected/7*7:nexus::portgrid::Start(selected),page_size=port?7:nexus::portgrid::PageSize;
        for(size_t i=start;i<std::min(Count(),start+page_size);++i){const auto r=port?nexus::portgrid::Rect{54,180+(i-start)*58.f,1172,54}:nexus::portgrid::Card(i);
            if(t->in_range({r.x,r.y,r.w,r.h})){if(selected==i)Open();else selected=i;break;}}
    }
    if(HasFocus()&&!port)PollIcons();SetSubHeading(std::to_string(Count()?selected+1:0)+" / "+std::to_string(Count()));
}
void Menu::Draw(NVGcontext* vg,Theme* theme){MenuBase::Draw(vg,theme);violet::Card(vg,{42,106,1196,533},false);Line(vg,62,117,1156,26,port?(mods_page?"Port mods / "_nx:"Manage port / "_nx)+port->name:"Ports & homebrew"_nx);Line(vg,62,151,1156,16,port?std::string(port->usb?"USB drive"_nx:"SD card"_nx)+" / "+(!port->nro.empty()?port->nro:port->roots.empty()?std::string{}:port->roots.front()):nexus::portgrid::Filter(filter));
    if(!port){
        const auto start=nexus::portgrid::Start(selected);
        for(size_t i=start;i<std::min(ports.size(),start+nexus::portgrid::PageSize);++i){const auto& item=ports[i];const auto r=nexus::portgrid::Card(i);violet::Card(vg,{r.x,r.y,r.w,r.h},i==selected,true);
            const auto image=CachedImage(IconKey(item));
            gfx::drawImage(vg,r.x+19,r.y+8,144,144,image>0?image:App::GetDefaultImage(),10);
            nvgBeginPath(vg);nvgRoundedRect(vg,r.x+r.w-57,r.y+12,47,25,7);nvgFillColor(vg,item.usb?nvgRGBA(102,54,181,245):nvgRGBA(35,82,157,245));nvgFill(vg);
            gfx::drawText(vg,r.x+r.w-33.5f,r.y+24.5f,15,nexusdialog::Text(),item.usb?"USB":"SD",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
            nvgSave(vg);nvgScissor(vg,r.x+10,r.y+157,r.w-20,39);gfx::drawTextBox(vg,r.x+10,r.y+157,17,r.w-20,nexusdialog::Text(),item.name.c_str());nvgRestore(vg);
        }
        if(ports.empty())Line(vg,72,230,1120,20,"No ports found. Press X to add an NRO, or R to change the filter."_nx);
        Line(vg,62,605,1150,16,ports.empty()?status:ports[selected].name+" / "+(ports[selected].usb?"USB":"SD")+" / "+(ports[selected].nro.empty()?ports[selected].roots.front():ports[selected].nro));return;
    }
    const auto start=selected/7*7;
    const char* labels[]={"Create HOME shortcut"_nx,"Manage mods"_nx,"Linked folders"_nx,"Move or copy port"_nx,"Delete port files"_nx,"Remove HOME shortcut"_nx};
    const char* descriptions[]={"Launch the NRO from HOME"_nx,"Import a folder or ZIP, enable or restore originals"_nx,"Review or add data folders"_nx,"Transfer the port between SD and USB"_nx,"Review every folder before permanent deletion"_nx,"Keep the port files and saved progress"_nx};
    for(size_t i=start;i<std::min(Count(),start+7);++i){const float y=180+(i-start)*58.f;violet::Card(vg,{54,y,1172,54},i==selected,true);std::string name,info,right;
        if(!port){name=ports[i].name;info=ports[i].nro.empty()?ports[i].roots.front():ports[i].nro;right=ports[i].application?"HOME linked"_nx:"NRO";}
        else if(mods_page){name=mods[i].name;info=mods[i].root+" / "+utils::formatSizeStorage(mods[i].bytes);right=mods[i].pending?"Recover"_nx:mods[i].active?"Enabled"_nx:"Disabled"_nx;}
        else {constexpr size_t data_actions[]={1,2,4,5};const auto action=port->nro.empty()?data_actions[i]:i;name=action==5&&port->nro.empty()?"Remove HOME application"_nx:labels[action];info=descriptions[action];right="Open"_nx;}
        Line(vg,74,y+7,955,20,name);Line(vg,74,y+33,955,14,info);Line(vg,1070,y+18,140,16,right);
    }
    if(!Count())Line(vg,72,210,1120,20,port?"No mods imported. Press X to add a folder or ZIP."_nx:"No ports found. Press X to add an NRO."_nx);Line(vg,62,605,1150,16,status);
}
}
