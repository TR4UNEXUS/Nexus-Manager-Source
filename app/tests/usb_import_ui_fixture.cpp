// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include "nexus/network_location_core.hpp"
#include "nexus/network_session_passwords.hpp"
#include "ui/usb_import_view.hpp"
using u64=std::uint64_t;using Result=std::uint32_t;using s64=std::int64_t;
constexpr Result Fault=123,Cancelled=456;
#include "network_socket_ui_mock.hpp"
#define MAKERESULT(module,description) (Result(10000+(module)+(description)))
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
#define R_TRY(expression) do { const auto result=(expression);if(R_FAILED(result))return result; } while(false)
#define R_UNLESS(expression,result) do { if(!(expression))return (result); } while(false)
namespace nexus {constexpr Result Invalid=20,Unavailable=21;}
namespace nexus::hats {inline bool compatible=true;bool SupportsPcLibrary(){return compatible;}}
namespace nexus {inline Result pc_firmware_rc{};Result CheckUsbFirmware(){return pc_firmware_rc;}}
inline Result ncmInitialize(){return 0;}inline void ncmExit(){}
#define ON_SCOPE_EXIT(expr) ((void)0)
namespace nexus::safety {enum class Operation{ReadOnly,MayWrite};}
namespace nexus::lifecycle {inline bool poll_allowed=true;bool CanPoll(){return poll_allowed;}}
// USB_LIBRARY_STARTUP_HEADER
namespace sphaira::nexusapp {
inline unsigned startup_starts{},startup_takes{},startup_stops{};
inline bool startup_available{};
void AddPcGames(std::shared_ptr<UsbImportScan> scan);
void StopUsbLibraryStartupCheck(){++startup_stops;startup_available=false;}
}
constexpr unsigned NcmContentMetaType_Application=0x80,NcmContentMetaType_Patch=0x81,NcmContentMetaType_AddOnContent=0x82,NcmContentMetaType_DataPatch=0x84;
struct NVGcontext{};struct NVGcolor{};
void nvgSave(NVGcontext*){}void nvgRestore(NVGcontext*){}
void nvgIntersectScissor(NVGcontext*,float,float,float,float){}
void nvgFontSize(NVGcontext*,float){}
float nvgTextBounds(NVGcontext*,float,float,const char* start,const char* end,float*){return float(end-start)*8;}
namespace sphaira::ui {
struct Vec4{float x,y,w,h;};struct Theme{};
enum class Button{A,B,X,Y,UP,DOWN,LEFT,RIGHT};
struct Controller{std::vector<Button> pressed;};
struct TouchInfo{bool is_clicked{};float x{},y{};bool in_range(Vec4 r)const{return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}};
struct Action{
    std::string label;std::function<void()> callback;
    Action()=default;Action(std::string name,std::function<void()> cb):label(std::move(name)),callback(std::move(cb)){}
    explicit Action(std::function<void()> cb):callback(std::move(cb)){}
};
namespace menu {
constexpr unsigned MenuFlag_None=0;
struct MenuBase{
    std::map<Button,Action> actions;bool focused{true},popped{};std::string subheading;
    MenuBase(const char*,unsigned){}virtual ~MenuBase()=default;
    virtual const char* GetShortTitle()const{return "";}
    virtual void Update(Controller* controller,TouchInfo*){
        // Deliberately dispatch every button in the frame: production callbacks
        // must reject actions after a previous callback loses focus or pops.
        for(auto button:controller->pressed)if(actions.contains(button))actions.at(button).callback();
    }
    virtual void Draw(NVGcontext*,Theme*){}
    void SetAction(Button button,Action action){actions[button]=std::move(action);}
    void SetPop(){popped=true;}bool ShouldPop()const{return popped;}bool HasFocus()const{return focused;}
    void SetSubHeading(std::string text){subheading=std::move(text);}
};
}
namespace gfx{
inline std::vector<std::string> drawn;
const char* getButton(Button button){switch(button){case Button::A:return "A";case Button::X:return "X";case Button::UP:return "Up";case Button::DOWN:return "Down";default:return "Button";}}
void drawText(NVGcontext*,float,float,float,NVGcolor,const char* text){drawn.emplace_back(text);}
}
namespace violet{void Card(NVGcontext*,Vec4,bool,bool=false){}}
namespace nexusdialog{NVGcolor Text(){return {};}void Button(NVGcontext*,Vec4,const char* text,bool){gfx::drawn.emplace_back(text);}}
enum class ProgressBoxOption{None};
struct OptionBox {
    using Callback=std::function<void(std::optional<s64>)>;
    std::string message,left,right;int selected{};Callback callback;
    OptionBox(std::string m,std::string a,Callback cb):message(std::move(m)),left(std::move(a)),callback(std::move(cb)){}
    OptionBox(std::string m,std::string a,std::string b,int index,Callback cb):message(std::move(m)),left(std::move(a)),right(std::move(b)),selected(index),callback(std::move(cb)){}
};
struct SidebarEntryCallback {
    std::string label;std::function<void()> callback;bool pop{};
    SidebarEntryCallback(std::string name,std::function<void()> cb,bool close=false):label(std::move(name)),callback(std::move(cb)),pop(close){}
};
struct Sidebar {
    enum class Side{RIGHT};std::string title,subtitle;
    std::vector<std::unique_ptr<SidebarEntryCallback>> entries;
    Sidebar(std::string name,std::string sub,Side):title(std::move(name)),subtitle(std::move(sub)){}
    template<class T,class... Args>T* Add(Args&&... args){auto row=std::make_unique<T>(std::forward<Args>(args)...);auto* result=row.get();entries.push_back(std::move(row));return result;}
};
struct ProgressBox {
    std::string action,title;std::function<Result(ProgressBox*)> work;std::function<void(Result)> done;
    nexus::safety::Operation operation;bool cancelled{};
    ProgressBox(int,std::string a,std::string t,std::function<Result(ProgressBox*)> w,std::function<void(Result)> d,
        ProgressBoxOption=ProgressBoxOption::None,nexus::safety::Operation op=nexus::safety::Operation::MayWrite)
        :action(std::move(a)),title(std::move(t)),work(std::move(w)),done(std::move(d)),operation(op){}
    Result ShouldExitResult()const{return cancelled?Cancelled:0;}
};
}
namespace sphaira {
struct App {
    static inline std::unique_ptr<ui::OptionBox> option;
    static inline std::unique_ptr<ui::Sidebar> sidebar;
    static inline std::unique_ptr<ui::ProgressBox> progress;
    static inline std::unique_ptr<ui::menu::MenuBase> menu;
    static inline unsigned option_pushes{},progress_pushes{};
    static inline bool applet{},exited{};
    static NVGcontext* GetVg(){static NVGcontext vg;return &vg;}
    static int GetDefaultImage(){return 1;}
    static bool IsApplet(){return applet;}
    static void PushErrorBox(Result,const std::string& message){Push<ui::OptionBox>(message,"Back",[](auto){});}
    static void Exit(){exited=true;}
    static void Focus(){if(menu)menu->focused=!option&&!progress;}
    template<class T,class... Args>static void Push(Args&&... args){
        if constexpr(std::is_same_v<T,ui::OptionBox>){++option_pushes;option=std::make_unique<T>(std::forward<Args>(args)...);}
        else if constexpr(std::is_same_v<T,ui::ProgressBox>){assert(!progress);++progress_pushes;progress=std::make_unique<T>(std::forward<Args>(args)...);}
        else if constexpr(std::is_base_of_v<ui::menu::MenuBase,T>)menu=std::make_unique<T>(std::forward<Args>(args)...);
        else static_assert(std::is_same_v<T,void>);
        Focus();
    }
    static void Push(std::unique_ptr<ui::Sidebar> value){sidebar=std::move(value);}
    template<class T>static void Push(std::unique_ptr<T> value){static_assert(std::is_base_of_v<ui::menu::MenuBase,T>);menu=std::move(value);Focus();}
    static void Press(std::optional<s64> choice){assert(option);auto callback=option->callback;option.reset();Focus();callback(choice);}
    static void Run(bool cancel=false){assert(progress);auto worker=std::move(progress);worker->cancelled=cancel;const auto result=worker->work(worker.get());Focus();worker->done(result);}
    static void Frame(std::vector<ui::Button> buttons={},ui::TouchInfo touch={}){assert(menu);ui::Controller controller{std::move(buttons)};menu->Update(&controller,&touch);}
    static void Key(ui::Button button){Frame({button});}
    static bool Has(const std::string& label){assert(menu);return std::any_of(menu->actions.begin(),menu->actions.end(),[&](const auto& pair){return pair.second.label==label;});}
    static void Select(const std::string& label){assert(menu);for(const auto& [button,action]:menu->actions)if(action.label==label){Key(button);return;}assert(false);}
    static std::string Draw(){assert(menu);ui::gfx::drawn.clear();NVGcontext vg;ui::Theme theme;menu->Draw(&vg,&theme);std::string text;for(const auto& line:ui::gfx::drawn)text+=line+'\n';return text;}
};
}
namespace sphaira::nexusapp {
enum class UsbImportStatus{Ready,AlreadyRegistered,Incomplete,TicketMissing};
struct Key{std::uint64_t id{};unsigned version{},type{NcmContentMetaType_Application};};
struct Record{Key key;std::uint64_t application_id{};};
struct Row{std::string name,reason;UsbImportStatus status{};Record record;std::string meta_path;};
using UsbImportTitle=Row;
struct UsbImportScan{u64 generation{31};std::vector<Row> titles;bool complete{};std::string detail;size_t nca_files{},unreadable_ncas{};};
struct UsbLibrarySummary{size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{},selected_items{};};
struct UsbLibraryPlan{UsbLibrarySummary summary;std::vector<bool> selected;std::vector<std::string> family_reason;};
static std::optional<UsbLibraryPlan> injected_plan;
UsbLibraryPlan PlanUsbLibrary(const UsbImportScan& scan){
    // The UI consumes an authoritative backend plan. Planner policy is tested
    // separately against its actual source; this fake supplies display data.
    if(injected_plan)return *injected_plan;
    UsbLibraryPlan plan;std::map<std::pair<std::uint64_t,unsigned>,unsigned> newest;
    for(const auto& row:scan.titles)newest[{row.record.key.id,row.record.key.type}]=std::max(newest[{row.record.key.id,row.record.key.type}],row.record.key.version);
    std::map<std::uint64_t,bool> affected,ready;
    for(const auto& row:scan.titles){const bool selected=row.status==UsbImportStatus::Ready&&row.record.key.version==newest[{row.record.key.id,row.record.key.type}];plan.selected.push_back(selected);plan.family_reason.emplace_back();if(selected){++plan.summary.selected_items;ready[row.record.application_id]=true;}if(row.status!=UsbImportStatus::AlreadyRegistered)affected[row.record.application_id]=true;}
    plan.summary.affected_games=affected.size();plan.summary.repairable_games=ready.size();plan.summary.blocked_games=affected.size()-ready.size();return plan;
}
struct UsbImportResult{std::vector<std::string> entries;std::vector<uint64_t> entry_applications;size_t imported{},skipped{};bool restart{},pending{},rolled_back{};std::string detail,report_path;};
static bool pending_flag{};static Result pending_rc{},scan_rc{},import_rc{},restore_rc{};
static unsigned pending_checks{},scans{},imports{},restores{},mutations{};
static UsbImportScan prepared_scan;static UsbImportResult import_result,restore_result;
const char* UsbImportStatusText(UsbImportStatus s){switch(s){case UsbImportStatus::Ready:return "Ready to import";case UsbImportStatus::AlreadyRegistered:return "Already registered";case UsbImportStatus::Incomplete:return "Incomplete on drive";case UsbImportStatus::TicketMissing:return "Ticket missing";}return "";}
std::string UsbImportPreview(const UsbImportScan&){return "Preview summary: source files stay on USB";}
Result UsbImportPending(bool& pending){++pending_checks;pending=pending_flag;return pending_rc;}
Result ScanUsbImports(ui::ProgressBox* p,UsbImportScan& result){++scans;assert(p->operation==nexus::safety::Operation::ReadOnly);if(p->cancelled){result.detail="Cancelled";return Cancelled;}result=prepared_scan;return scan_rc;}
static bool quick_import{};
Result ImportUsbLibrary(ui::ProgressBox* p,const UsbImportScan& scan,UsbImportResult& result,bool quick=false){quick_import=quick;++imports;assert(startup_stops&&!startup_available);assert(p->operation==nexus::safety::Operation::MayWrite);assert(scan.complete);if(p->cancelled){result.detail="Cancelled before registration";return Cancelled;}++mutations;result=import_result;return import_rc;}
Result RestoreUsbImport(ui::ProgressBox* p,UsbImportResult& result){++restores;assert(p->operation==nexus::safety::Operation::MayWrite);if(p->cancelled){result.detail="Restore cancelled before writes";return Cancelled;}++mutations;result=restore_result;return restore_rc;}
enum class UsbTicketPrepareStatus{Ready,AlreadyPrepared,NeedsBackup,Incomplete};
struct UsbTicketPrepareTitle{std::string name,reason,identity;UsbTicketPrepareStatus status{};};
struct UsbTicketPreparePlan{std::vector<UsbTicketPrepareTitle> titles;size_t tickets_to_save{};bool complete{};std::string detail;};
struct UsbTicketPrepareResult{std::vector<std::string> entries;size_t prepared{},already_prepared{},needs_backup{},skipped{},saved{};Result result{};std::string detail,report_path;};
static UsbTicketPreparePlan prepare_plan;static UsbTicketPrepareResult prepare_result;
static unsigned plans{},prepares{};static Result plan_rc{},prepare_rc{};
const char* UsbTicketPrepareStatusText(UsbTicketPrepareStatus s){switch(s){case UsbTicketPrepareStatus::Ready:return "Ready to prepare";case UsbTicketPrepareStatus::AlreadyPrepared:return "Already prepared";case UsbTicketPrepareStatus::NeedsBackup:return "Needs original backup";case UsbTicketPrepareStatus::Incomplete:return "Skipped";}return "";}
std::string UsbTicketPreparePreview(const UsbTicketPreparePlan&){return "Supported setup only. Game files and saves will not be copied.";}
Result PlanUsbTicketPreparation(ui::ProgressBox* p,const UsbImportScan& scan,UsbTicketPreparePlan& plan){++plans;assert(scan.complete&&p->operation==nexus::safety::Operation::ReadOnly);if(p->cancelled){plan.detail="Cancelled";return Cancelled;}plan=prepare_plan;return plan_rc;}
Result PrepareUsbTickets(ui::ProgressBox* p,const UsbTicketPreparePlan& plan,UsbTicketPrepareResult& result){++prepares;assert(plan.complete&&p->operation==nexus::safety::Operation::MayWrite);if(p->cancelled){result.detail="Cancelled before saving";return Cancelled;}++mutations;result=prepare_result;return prepare_rc;}
}
namespace sphaira::nexusapp {
void DisplayPcUsbImport(u64 only_application=0);
struct PcRemoval {std::uint64_t game{};std::string filename{"change.swap"};};inline std::vector<PcRemoval> pc_removals;inline unsigned pc_applied{};
inline std::shared_ptr<std::vector<PcRemoval>> pending_startup_removals;
inline unsigned resumed_additions{};
std::shared_ptr<std::vector<PcRemoval>> TakeDetectedPcRemovals(){return std::move(pending_startup_removals);}
void ResumePcInstallDetection(){++resumed_additions;}
Result ListPcRemovals(ui::ProgressBox*,std::vector<PcRemoval>& out){out=pc_removals;return 0;}
Result FinishPcRemoval(ui::ProgressBox*,const PcRemoval&,bool& changed){++pc_applied;changed=true;return 0;}
}
struct Thread{void(*work)(void*){};void* arg{};};
Result threadStart(Thread* t){t->work(t->arg);return 0;}
Result threadWaitForExit(Thread*){return 0;}
void threadClose(Thread*){}
void fatalThrow(Result){std::abort();}
inline unsigned allocated_icons{},released_icons{};
int nvgCreateImageMem(NVGcontext*,int,unsigned char*,size_t){return 10+ ++allocated_icons;}
void nvgDeleteImage(NVGcontext*,int image){assert(image>1);++released_icons;}
namespace sphaira::utils {
Result CreateThread(Thread* thread,void(*work)(void*),void* arg,size_t){*thread={work,arg};return 0;}
}
namespace sphaira::ui {
inline std::vector<u64> requested_icons;
namespace gfx {void drawImage(NVGcontext*,float,float,float,float,int,int){}}
}
namespace sphaira::nexusapp {
using UsbImportFamily=std::vector<const UsbImportTitle*>;
struct UsbImportArtwork{std::string name;std::vector<unsigned char> icon;};
UsbImportArtwork ReadUsbImportArtwork(u64 generation,const UsbImportFamily& family,const std::atomic_bool* cancel){
    assert(generation==31 && !family.empty() && !cancel->load());
    const auto app=family.front()->record.application_id;
    for(const auto* row:family)assert(row->record.application_id==app);
    ui::requested_icons.push_back(app);
    return {family.front()->name, app==300?std::vector<unsigned char>{}:std::vector<unsigned char>{1}};
}
}
// USB_ICON_CACHE_PRODUCTION
// USB_IMPORT_UI_PRODUCTION
namespace networkcheck {
using u32=std::uint32_t;using Connection=nexus::network::Connection;
constexpr Result IoError=10;constexpr int NifmServiceType_User=1;
constexpr const char* ConfigPath="sdmc:/config/nexusmanager/network.ini";
std::vector<std::string> events;Result sockets_rc{},network_rc{},address_rc{},directory_rc{};
nexus::network::SessionPasswords session_passwords;
u32 address=0x01010101;std::map<std::string,std::string> disk;
std::vector<std::pair<std::string,std::optional<std::string>>> writes;
Result socketInitialize(const SocketInitConfig* config){assert(config->tcp_rx_buf_size==512*1024);events.push_back("open sockets");return sockets_rc;}
Result nifmInitialize(int){events.push_back("open nifm");return network_rc;}
Result nifmGetCurrentIpAddress(u32* value){events.push_back("get address");*value=address;return address_rc;}
void socketExit(){events.push_back("close sockets");}
void nifmExit(){events.push_back("close nifm");}
void UmountNeworkDevice(const std::string&){events.push_back("unmount");}
namespace fs {using FsPath=std::string;struct FsNativeSd {Result CreateDirectoryRecursively(const char*){return directory_rc;}};}
int ini_puts(const char*,const char* key,const char* value,const char* path){
    assert(std::string(path)==ConfigPath);if(!key){disk.clear();return 1;}writes.emplace_back(key,value?std::optional<std::string>{value}:std::nullopt);
    if(value)disk[key]=value;else disk.erase(key);return 1;
}
int ini_haskey(const char*,const char* key,const char* path){assert(std::string(path)==ConfigPath);return disk.contains(key);}
int ini_gets(const char*,const char* key,const char* fallback,char* output,int size,const char* path){
    assert(std::string(path)==ConfigPath);const auto value=disk.contains(key)?disk.at(key):std::string(fallback);
    std::snprintf(output,static_cast<size_t>(size),"%s",value.c_str());return static_cast<int>(std::char_traits<char>::length(output));
}
int ini_browse(int (*callback)(const char*,const char*,const char*,void*),void* argument,const char* path){
    assert(std::string(path)==ConfigPath);
    for(const auto& [key,value]:disk)if(!callback("NAS",key.c_str(),value.c_str(),argument))break;
    return 1;
}
// NETWORK_SESSION_PRODUCTION
void Verify(){
    events.clear();{Session session;}assert(events.empty()); // no startup service initialization
    for(unsigned stage=0;stage<4;++stage){
        events.clear();sockets_rc=stage==0?Fault:0;network_rc=stage==1?Fault:0;address_rc=stage==2?Fault:0;
        {Session session;auto result=session.Open();assert((stage==3)==(result==0));if(result==0)session.mount="nexusnet1:/";}
        const std::vector<std::vector<std::string>> expected{
            {"open sockets"},
            {"open sockets","open nifm","close sockets"},
            {"open sockets","open nifm","get address","close nifm","close sockets"},
            {"open sockets","open nifm","get address","unmount","close nifm","close sockets"}};
        assert(events==expected[stage]);
    }
    events.clear();address=0;{Session session;assert(session.Open()==nexus::Unavailable);}assert(events.back()=="close sockets");address=1;
    const Connection connection{"NAS","SMB","192.168.0.10","Games","user","WORKGROUP"};
    disk={{"pass","old-secret"},{"password","old-secret"}};writes.clear();assert(Save(connection)==0);
    assert(disk.size()==6&&!disk.contains("pass")&&!disk.contains("password")&&disk.at("remember_password")=="1");
    assert(disk.at("protocol")=="SMB"&&disk.at("host")==connection.host&&disk.at("share")==connection.share&&disk.at("user")==connection.user&&disk.at("domain")==connection.domain);
    for(const auto& [key,value]:writes)if(value){assert(key!="pass"&&key!="password");assert(value->find("old-secret")==std::string::npos);}
    assert(Url(connection).find(connection.user)==std::string::npos);
    auto invalid=connection;invalid.host="user:password@host";const auto previous=writes.size();assert(Save(invalid)==nexus::Invalid&&writes.size()==previous);
    directory_rc=Fault;assert(Save(connection)==Fault&&writes.size()==previous);
}
}
// USB_IMPORT_GUIDED_CASES
