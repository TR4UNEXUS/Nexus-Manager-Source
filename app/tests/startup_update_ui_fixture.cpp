// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "nexus/lifecycle.hpp"
#include "nexus/release.hpp"
using Result=std::uint32_t;using s64=std::int64_t;using u32=std::uint32_t;
using u64=std::uint64_t;using u8=std::uint8_t;
constexpr Result Fault=123,Cancelled=456;
#define R_FAILED(rc) ((rc)!=0)
static bool standalone{};
bool envIsNso(){return standalone;}
namespace nexus {enum class Target{Sd};const char* TargetName(Target){return "SD";}}
namespace nexus::safety {enum class Operation{ReadOnly,MayWrite};}
namespace sphaira::ui {
enum class ProgressBoxOption{None};
struct OptionBox {
    using Callback=std::function<void(std::optional<s64>)>;
    std::string message,left,right;int selected{};Callback callback;
    OptionBox(std::string m,std::string a,Callback cb):message(std::move(m)),left(std::move(a)),callback(std::move(cb)){}
    OptionBox(std::string m,std::string a,std::string b,int index,Callback cb):message(std::move(m)),left(std::move(a)),right(std::move(b)),selected(index),callback(std::move(cb)){}
};
struct ProgressBox {
    std::string action,title;std::function<Result(ProgressBox*)> work;std::function<void(Result)> done;
    nexus::safety::Operation operation;bool cancelled{};
    ProgressBox(int,std::string a,std::string t,std::function<Result(ProgressBox*)> w,std::function<void(Result)> d,
        ProgressBoxOption=ProgressBoxOption::None,nexus::safety::Operation op=nexus::safety::Operation::MayWrite)
        :action(std::move(a)),title(std::move(t)),work(std::move(w)),done(std::move(d)),operation(op){}
};
}
namespace sphaira {
struct App {
    static inline std::unique_ptr<ui::OptionBox> option;
    static inline std::unique_ptr<ui::ProgressBox> progress;
    static inline unsigned options{},progresses{},exits{};
    static inline bool external_modal{},exited{};
    static bool Modal(){return option || progress || external_modal;}
    static void Exit(){++exits;exited=true;}
    static void* GetVg(){return nullptr;}
    template<class T,class... Args>static void Push(Args&&... args){
        assert(!Modal());
        if constexpr(std::is_same_v<T,ui::OptionBox>){++options;option=std::make_unique<T>(std::forward<Args>(args)...);}
        else if constexpr(std::is_same_v<T,ui::ProgressBox>){++progresses;progress=std::make_unique<T>(std::forward<Args>(args)...);}
        else static_assert(std::is_same_v<T,void>);
    }
    static void Press(std::optional<s64> choice){assert(option);auto callback=option->callback;option.reset();callback(choice);}
    static void Run(bool cancel=false){assert(progress);auto worker=std::move(progress);worker->cancelled=cancel;const auto result=worker->work(worker.get());worker->done(result);}
    static void Reset(){option.reset();progress.reset();options=progresses=exits=0;external_modal=exited=false;}
};
}
namespace nexus::updates {
// VERSION_CODE_PRODUCTION
static unsigned starts{},jobs{},takes{},stops{},checks{},installs{},restarts{};
static bool attempted{},stopped{},ready{};
static Artifact started_kind{};
static Release prepared,manual,installed;
static Result check_result{},install_result{},restart_result{},last_install_result{};
static std::vector<std::string> events;
void StartStartupCheck(Artifact kind){++starts;events.push_back("start");if(attempted)return;attempted=true;++jobs;started_kind=kind;}
bool TakeStartupRelease(Release& release){++takes;if(!ready)return false;ready=false;release=prepared;return true;}
void StopStartupCheck(){++stops;attempted=stopped=true;events.push_back("stop");}
Result Check(sphaira::ui::ProgressBox* p,Release& release,std::string& detail){
    assert(stopped);assert(p->operation==nexus::safety::Operation::ReadOnly);
    ++checks;events.push_back("check");release=manual;detail="Manual check result";return p->cancelled?Cancelled:check_result;
}
Result Install(sphaira::ui::ProgressBox* p,const Release& release,std::string& detail){
    assert(p->operation==nexus::safety::Operation::MayWrite);
    ++installs;events.push_back("install");installed=release;detail="Verified update ready";
    return last_install_result=p->cancelled?Cancelled:install_result;
}
Result RestartUpdatedManager(std::string& detail){
    assert(installs==1&&last_install_result==0&&events.back()=="install");
    assert(!sphaira::App::Modal());++restarts;events.push_back("restart");
    if(restart_result){detail="Installed, but automatic restart failed";return restart_result;}
    sphaira::App::Exit();return 0;
}
Release Newer(Artifact kind=Artifact::Nro){Release release;release.version="99.1.0";release.kind=kind;release.tag="v99.1.0";release.url="test://verified-asset";release.sha256="verified-digest";release.size=76543;return release;}
void Reset(){starts=jobs=takes=stops=checks=installs=restarts=0;attempted=stopped=ready=false;started_kind=Artifact::Nro;prepared=manual=Newer();installed={};check_result=install_result=restart_result=last_install_result=0;events.clear();}
}

// Unrelated menu rendering/navigation dependencies remain inert. The production
// constructor and full Update method execute, including their early-return order.
namespace sphaira::ui::menu::nexusmenu {
struct Rect{float x{},y{},w{},h{};};
struct Controller{std::function<void()> input;};
struct TouchInfo{bool is_clicked{};bool in_range(Rect){return false;}};
enum class Button{A,B,X,SELECT,LEFT,RIGHT,UP,DOWN};
struct Action{
    std::function<void()> callback;
    explicit Action(std::function<void()> cb):callback(std::move(cb)){}
    Action(const char*,std::function<void()> cb):callback(std::move(cb)){}
};
struct MenuBase {
    bool focused{true};unsigned input_updates{};std::map<Button,Action> actions;
    MenuBase(const char*,u32){}
    bool HasFocus()const{return focused&&!App::Modal();}
    void SetAction(Button button,Action action){actions.insert_or_assign(button,std::move(action));}
    void SetTitleSubHeading(const std::string&){}
    void SetSubHeading(const std::string&){}
    void Update(Controller* controller,TouchInfo*){++input_updates;if(controller&&controller->input)controller->input();}
};
namespace view {
constexpr size_t HomeCount=4,MoreCount=8,PageSize=8;
size_t Vertical(size_t value,size_t,bool){return value;}
size_t HomeVertical(size_t value,bool){return value;}
size_t MoreVertical(size_t value,bool){return value;}
size_t PageStart(size_t value){return value/PageSize*PageSize;}
Rect HomeCard(size_t){return {};}
Rect MoreCard(size_t){return {};}
Rect Card(size_t){return {};}
Rect PrimaryAction(){return {};}
}
struct File{bool directory{};};
struct Game{u64 id{};std::string homebrew_path,name,display_version;int badge_state{};std::string CacheKey()const{return name;}};
struct Packages{std::vector<int> Entries()const{return {};}};
namespace title {
enum class NacpLoadStatus{Loaded};
struct Data{std::string display_version;NacpLoadStatus status;struct{char name[1]{};}lang;std::vector<u8> icon;};
void PushAsync(u64){}
const Data* GetAsync(u64){return nullptr;}
}
struct Image{std::vector<u8> data;int w{},h{};};
Image ImageLoadIcon(const std::vector<u8>&){return {};}
Image ImageResize(const std::vector<u8>&,int,int,int,int){return {};}
namespace nexus_badge{void Overlay(u8*,size_t,int){}}
int nvgCreateImageRGBA(void*,int,int,int,const u8*){return 0;}
void nvgDeleteImage(void*,int){}
namespace nexusapp{void DisplayUsbImport(){}}
struct Menu:MenuBase {
    enum class Page{Home,More,Installed,Files,Homebrew,Cleanup,Queue,Results,Mods,ModFolders};
    Page page=Page::Home;bool cleanup_tickets{};nexus::Target cleanup_target{};size_t selected{};bool actions_dirty{true},refresh_pending{},usb_source{};
    // STARTUP_UPDATE_UI_STATE
    std::string path="/",mod_folder;std::vector<File> files;std::vector<Game> games;Packages packages;
    std::set<u64> title_requested;std::map<std::string,int> icons;std::set<std::string> icon_checked;
    unsigned reports{},activations{};Result reported{};std::string report;
    explicit Menu(u32 flags=0);
    void Update(Controller*,TouchInfo*);
    void CheckForUpdates();bool PollStartupUpdate();void ShowUpdatePrompt(const nexus::updates::Release&);
    // The USB prompt is exercised against its actual body in the import UI
    // fixture. Keep this independent updater fixture's new sibling idle.
    bool PollUsbLibraryStartup(){return false;}
    void OpenUsbRepair(){nexusapp::DisplayUsbImport();}
    void Report(Result result,const std::string& detail){++reports;reported=result;report=detail;}
    void SetupActions(){actions_dirty=false;}void Refresh(){}void Back(){}void Updates(){}
    void Activate(){++activations;page=Page::Installed;}void TogglePackage(){}void GameActions(bool=false){}
    void PollIcon(){}void QueueIcon(const Game&){}size_t Count()const{return 0;}
};
// STARTUP_UPDATE_UI_PRODUCTION
}

using sphaira::App;
using sphaira::ui::menu::nexusmenu::Menu;
using sphaira::ui::menu::nexusmenu::Controller;
using sphaira::ui::menu::nexusmenu::TouchInfo;
using nexus::updates::Artifact;
namespace updates=nexus::updates;
void Reset(){App::Reset();updates::Reset();standalone=false;nexus::lifecycle::foreground=true;nexus::lifecycle::stopping=false;}
void Tick(Menu& menu){Controller controller;TouchInfo touch;menu.Update(&controller,&touch);}
void Publish(Artifact kind=Artifact::Nro){updates::prepared=updates::Newer(kind);updates::ready=true;}
void ExpectPrompt(Artifact kind){
    assert(App::option&&App::option->left=="Not now"&&App::option->selected==0);
    assert(App::option->right=="Update and restart");
    assert(App::option->message.find(updates::prepared.version)!=std::string::npos);
    if(kind==Artifact::Nsp){
        assert(App::option->message.find("install")!=std::string::npos);
        assert(App::option->message.find("reopen automatically")!=std::string::npos);
        assert(App::option->message.find("install the downloaded NSP")==std::string::npos);
    }
    assert(updates::installs==0&&updates::restarts==0&&!App::progress);
}
int main(){
    // Construction performs no blocking check, network admission or UI push.
    Reset();{Menu menu;assert(updates::starts==0&&updates::checks==0&&updates::installs==0&&!App::Modal());}

    // Startup cannot begin beneath a modal, while unfocused/backgrounded, or
    // during shutdown. It starts once on the first eligible foreground frame.
    for(unsigned gate=0;gate<4;++gate){
        Reset();Menu menu;
        if(gate==0)menu.focused=false;if(gate==1)App::external_modal=true;
        if(gate==2)nexus::lifecycle::foreground=false;if(gate==3)nexus::lifecycle::stopping=true;
        Tick(menu);assert(updates::starts==0&&updates::takes==0&&!App::option);
        menu.focused=true;App::external_modal=false;nexus::lifecycle::foreground=true;nexus::lifecycle::stopping=false;
        Tick(menu);Tick(menu);assert(updates::starts==1&&updates::jobs==1&&updates::checks==0&&updates::installs==0);
    }

    // A result arriving during any non-Home task stays pending until Home.
    for(auto page:{Menu::Page::More,Menu::Page::Installed,Menu::Page::Files,Menu::Page::Homebrew,
                  Menu::Page::Cleanup,Menu::Page::Queue,Menu::Page::Results,Menu::Page::Mods,Menu::Page::ModFolders}){
        Reset();Menu menu;menu.page=page;Tick(menu);Publish();Tick(menu);
        assert(updates::starts==1&&updates::takes==0&&updates::ready&&!App::option);
        menu.page=Menu::Page::Home;Tick(menu);ExpectPrompt(Artifact::Nro);App::Press(0);
        const auto takes=updates::takes;Tick(menu);assert(!App::option&&updates::takes==takes&&updates::installs==0);
    }

    // An already running check may complete under a sidebar/progress dialog or
    // in the background. None of those states may consume the queued release.
    for(unsigned gate=0;gate<4;++gate){
        Reset();Menu menu;Tick(menu);Publish();const auto takes=updates::takes;
        if(gate==0)menu.focused=false;if(gate==1)App::external_modal=true;
        if(gate==2)nexus::lifecycle::foreground=false;if(gate==3)nexus::lifecycle::stopping=true;
        Tick(menu);assert(updates::takes==takes&&updates::ready&&!App::option);
        menu.focused=true;App::external_modal=false;nexus::lifecycle::foreground=true;nexus::lifecycle::stopping=false;
        Tick(menu);ExpectPrompt(Artifact::Nro);App::Press(0);
    }

    // Once displayed, the incoming frame cannot activate a Home tile beneath
    // the new prompt. Declining or dismissing never queues a download.
    for(auto kind:{Artifact::Nro,Artifact::Nsp})for(auto choice:{std::optional<s64>{},std::optional<s64>{0},std::optional<s64>{2}}){
        Reset();Menu menu;Tick(menu);Publish(kind);Controller controller{[&]{menu.Activate();}};TouchInfo touch;
        const auto processed=menu.input_updates;menu.Update(&controller,&touch);
        assert(menu.input_updates==processed&&menu.activations==0&&menu.page==Menu::Page::Home);
        ExpectPrompt(kind);App::Press(choice);Tick(menu);Tick(menu);
        assert(App::options==1&&App::progresses==0&&updates::installs==0&&updates::restarts==0&&!App::exited);
        Menu reopened;Tick(reopened);assert(updates::jobs==1&&App::options==1);
    }

    // Both editions install and immediately request restart after one explicit
    // confirmation. The prompt follows the verified release artifact.
    for(auto kind:{Artifact::Nro,Artifact::Nsp}){
        Reset();standalone=kind==Artifact::Nsp;Menu menu;Tick(menu);assert(updates::started_kind==kind);
        Publish(kind);standalone=!standalone;Tick(menu);ExpectPrompt(kind);
        if(kind==Artifact::Nro)assert(App::option->message.find("backup")!=std::string::npos);
        App::Press(1);assert(App::progress&&updates::installs==0&&updates::restarts==0&&!App::exited);App::Run();
        assert(updates::installs==1&&updates::installed.kind==kind);
        assert(updates::installed.url==updates::prepared.url&&updates::installed.sha256==updates::prepared.sha256&&updates::installed.size==updates::prepared.size);
        assert(updates::restarts==1&&updates::events.back()=="restart");
        assert(App::options==1&&!App::Modal()&&App::exited&&App::exits==1&&menu.reports==0);
        Tick(menu);Tick(menu);assert(updates::installs==1&&updates::restarts==1);
    }

    // Errors and cancellation never request restart, for either edition.
    for(auto kind:{Artifact::Nro,Artifact::Nsp})for(bool cancelled:{false,true}){
        Reset();Menu menu;Tick(menu);Publish(kind);Tick(menu);App::Press(1);
        if(!cancelled)updates::install_result=Fault;App::Run(cancelled);
        assert(menu.reports==1&&menu.reported==(cancelled?Cancelled:Fault)&&!App::exited&&!App::Modal());
        assert(updates::installs==1&&updates::restarts==0&&App::exits==0);
        Tick(menu);assert(updates::restarts==0);
    }

    // An installed update whose restart request fails stays in the UI with the
    // actual restart error. It neither claims exit nor retries automatically.
    for(auto kind:{Artifact::Nro,Artifact::Nsp}){
        Reset();Menu menu;Tick(menu);Publish(kind);Tick(menu);App::Press(1);
        updates::restart_result=Fault;App::Run();
        assert(updates::installs==1&&updates::restarts==1&&menu.reports==1&&menu.reported==Fault);
        assert(menu.report=="Installed, but automatic restart failed"&&!App::Modal()&&!App::exited&&App::exits==0);
        Tick(menu);Tick(menu);assert(updates::restarts==1&&menu.reports==1);
    }

    // Manual checking joins startup before its worker and owns the only prompt,
    // whether invoked before startup begins or with a completed result queued.
    for(bool startup_begun:{false,true})for(auto kind:{Artifact::Nro,Artifact::Nsp}){
        Reset();Menu menu;if(startup_begun)Tick(menu);Publish(kind);updates::manual=updates::prepared;
        menu.CheckForUpdates();assert(updates::stops==1&&updates::checks==0&&App::progress);
        const auto takes=updates::takes;Tick(menu);assert(updates::takes==takes);
        App::Run();assert(updates::checks==1&&updates::events.back()=="check");ExpectPrompt(kind);
        App::Press(0);Tick(menu);Tick(menu);assert(App::options==1&&updates::takes==takes&&updates::installs==0);
    }

    // Manual checks enter that same install-and-restart callback in both editions.
    for(auto kind:{Artifact::Nro,Artifact::Nsp}){
        Reset();Menu menu;updates::manual=updates::prepared=updates::Newer(kind);
        menu.CheckForUpdates();App::Run();ExpectPrompt(kind);App::Press(1);App::Run();
        assert(updates::checks==1&&updates::installs==1&&updates::restarts==1);
        assert(App::options==1&&App::progresses==2&&App::exits==1&&menu.reports==0);
    }

    // An up-to-date/manual error/cancel result also consumes the startup UI
    // opportunity; a pending automatic result must not surprise the user later.
    for(unsigned result=0;result<3;++result){
        Reset();Menu menu;Tick(menu);Publish();updates::manual.version=updates::Version;
        if(result==1)updates::check_result=Fault;
        menu.CheckForUpdates();App::Run(result==2);
        if(result==0){assert(App::option&&App::option->message.find("up to date")!=std::string::npos);App::Press(0);}
        else assert(menu.reports==1&&menu.reported==(result==1?Fault:Cancelled));
        const auto options=App::options,takes=updates::takes;Tick(menu);
        assert(App::options==options&&updates::takes==takes&&!App::option&&updates::installs==0);
    }
    std::cout<<"Startup update UI tests passed (production startup/manual checks, confirmed install, automatic restart and fault handling)\n";
}
