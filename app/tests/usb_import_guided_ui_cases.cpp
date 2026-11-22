// SPDX-License-Identifier: GPL-3.0-or-later
using sphaira::App;using namespace sphaira::nexusapp;
using UiButton=sphaira::ui::Button;
unsigned cases{};
void Case(){++cases;}
bool Contains(const std::string& text,const std::string& needle){return text.find(needle)!=std::string::npos;}
Row MakeRow(const char* name,const char* reason,UsbImportStatus status,std::uint64_t id,unsigned version,unsigned type,std::uint64_t family){
    Row row;row.name=name;row.reason=reason;row.status=status;row.record={{id,version,type},family};row.meta_path="usb:/registered/example.cnmt.nca";return row;
}
void Reset(){
    App::option.reset();App::sidebar.reset();App::progress.reset();App::menu.reset();App::applet=App::exited=false;
    App::option_pushes=App::progress_pushes=0;
    injected_plan.reset();startup_result={};startup_starts=startup_takes=startup_stops=0;startup_available=false;nexus::lifecycle::poll_allowed=true;
    pending_flag=false;pending_rc=scan_rc=import_rc=restore_rc=plan_rc=prepare_rc=0;
    pending_checks=scans=imports=restores=mutations=plans=prepares=0;
    prepared_scan={};prepared_scan.complete=true;prepared_scan.detail="Scan complete";
    prepared_scan.titles={MakeRow("Ready game","Complete files",UsbImportStatus::Ready,0x0100000000001000ULL,0,NcmContentMetaType_Application,0x0100000000001000ULL),
        MakeRow("Existing game","Registered here",UsbImportStatus::AlreadyRegistered,0x0100000000002000ULL,0,NcmContentMetaType_Application,0x0100000000002000ULL),
        MakeRow("Incomplete update","One file is missing",UsbImportStatus::Incomplete,0x0100000000001800ULL,42,NcmContentMetaType_Patch,0x0100000000001000ULL),
        MakeRow("DLC","Reinstall from original backup",UsbImportStatus::TicketMissing,0x0100000000001001ULL,1,NcmContentMetaType_AddOnContent,0x0100000000001000ULL)};
    import_result={{"Ready game imported\nVerification and registration complete","Other records skipped\nThe files are unchanged"},1,3,true,false,false,"Import complete","SD:/report.txt"};
    restore_result={{"Registration restored"},0,0,true,false,true,"Backup restored","SD:/restore.txt"};
    prepare_plan={{{"Ready game - Base game","A supported licence is available","ID 0100000000001000 version 0",UsbTicketPrepareStatus::Ready},
        {"Existing game - Base game","Existing licence copy is valid","ID 0100000000002000 version 0",UsbTicketPrepareStatus::AlreadyPrepared},
        {"DLC - DLC","The original backup is required","ID 0100000000001001 version 1",UsbTicketPrepareStatus::NeedsBackup}},1,true,"Setup checked"};
    prepare_result={{"Ready game prepared\nPermission saved","DLC needs original backup"},1,1,1,0,1,0,"Setup saved","SD:/prepare-report.txt"};
}
void Start(){DisplayUsbImport();assert(App::progress&&App::progress->operation==nexus::safety::Operation::ReadOnly);assert(!pending_checks&&!scans&&!mutations);App::Run();}
void Scan(){Start();assert(App::menu&&!App::option&&!App::progress&&pending_checks==1&&scans==1&&!imports&&!mutations);}
void Details(const std::string& expected){App::Key(UiButton::A);assert(App::option&&App::option->left=="Back"&&App::option->right.empty());if(!Contains(App::option->message,expected))std::cerr<<"Case "<<cases<<" expected detail: "<<expected<<"\nActual: "<<App::option->message<<'\n';assert(Contains(App::option->message,expected));App::Press({});}
void BeginPlan(){Scan();App::Select("Prepare USB");assert(App::option&&App::option->right=="Check preparation"&&App::option->selected==0);assert(Contains(App::option->message,"original SD card/system"));App::Press(1);assert(App::progress&&App::progress->operation==nexus::safety::Operation::ReadOnly);assert(!plans&&!prepares&&!mutations);}
void Plan(){BeginPlan();App::Run();}
void AcceptOnce(){
    assert(App::option);auto repeated=App::option->callback;auto* owner=App::menu.get();const auto workers=App::progress_pushes;
    App::Press(1);assert(owner->ShouldPop()&&App::progress&&App::progress_pushes==workers+1);
    repeated(1);assert(App::progress_pushes==workers+1&&!mutations);
    const auto dialogs=App::option_pushes;App::Frame({UiButton::X,UiButton::Y,UiButton::A},{true,1000,150});assert(App::option_pushes==dialogs);
}
void CancelOnce(std::optional<s64> choice){
    assert(App::option);auto repeated=App::option->callback;auto* owner=App::menu.get();const auto workers=App::progress_pushes;
    App::Press(choice);repeated(1);assert(!owner->ShouldPop()&&owner->HasFocus()&&!App::progress&&App::progress_pushes==workers&&!mutations);
}
namespace librarycheck {
enum class Button{A,B,X,Y,R,SELECT,START};
struct Action{std::string label;std::function<void()> callback;Action()=default;Action(std::function<void()> cb):callback(std::move(cb)){}Action(std::string text,std::function<void()> cb):label(std::move(text)),callback(std::move(cb)){};};
static unsigned entries{},managed{},moved{},refreshed{},helped{},exited{};
namespace nexusapp{void DisplayUsbImport(){++entries;}}
struct App{static void Exit(){++exited;}};
struct Menu{
    size_t selected{};std::vector<int> games;std::map<Button,Action> actions;
    void SetAction(Button b,Action a){actions[b]=std::move(a);}
    const char* PrimaryLabel(int){return "Move game";}
    void GameActions(bool move=false){if(move)++moved;else ++managed;}
    void Refresh(){++refreshed;}void Updates(){++helped;}
    void OpenUsbRepair(){++entries;}
    void Setup(){// LIBRARY_INSTALLED_ACTIONS
    }
};
struct Rect{float x,y,w,h;};
struct Touch{float x,y;bool in_range(Rect r)const{return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}};
void OpenUsbRepair(){++entries;}
void TouchEntry(Touch* touch){// LIBRARY_IMPORT_TOUCH
}
struct DrawRecord{Rect rect;std::string text;};static std::vector<DrawRecord> draws;
constexpr int NVG_ALIGN_LEFT=1,NVG_ALIGN_TOP=2;
void Text(int,float,float,float,int,int,const char*){}
namespace gfx{const char* getButton(Button button){assert(button==Button::R);return "[Switch R]";}}
namespace nexusdialog{void Button(int,Rect rect,const char* text,bool){draws.push_back({rect,text});}}
void DrawHeader(){const int vg{},fg{};// LIBRARY_IMPORT_DRAW
}
void Verify(){
    for(bool empty:{false,true}){Menu menu;if(!empty)menu.games.push_back(1);menu.Setup();
        assert(menu.actions.size()==5&&!menu.actions.contains(Button::X)); // B is shared by all pages; Move was removed.
        menu.actions.at(Button::R).callback();
        menu.actions.at(Button::A).callback();menu.actions.at(Button::Y).callback();menu.actions.at(Button::SELECT).callback();menu.actions.at(Button::START).callback();}
    assert(entries==2&&managed==2&&!moved&&refreshed==2&&helped==2&&exited==2);
    draws.clear();DrawHeader();assert(draws.size()==1);const auto r=draws[0].rect;
    assert(r.x==850&&r.y==110&&r.w==376&&r.h==46&&r.x+r.w<=1226&&r.y+r.h<174);
    assert(draws[0].text=="[Switch R]  Repair USB games");
    Touch inside{r.x+r.w/2,r.y+r.h/2};TouchEntry(&inside);assert(entries==3);
    for(const Touch outside:std::vector<Touch>{{r.x-1,r.y},{r.x,r.y-1},{r.x+r.w,r.y},{r.x,r.y+r.h}}){auto touch=outside;TouchEntry(&touch);}assert(entries==3);
}
}
namespace startupcheck {
using sphaira::App;using sphaira::ui::OptionBox;
namespace nexusapp {inline unsigned guided{};void DisplayGuidedUsbRepair(){++guided;}}
enum class Page {Home,Installed,Files,Results};
struct Menu {
    bool startup_usb_started{},startup_usb_handled{},refresh_pending{},focused{true},popped{};
    Page page{Page::Home};
    bool HasFocus()const{return focused&&!App::option&&!App::progress&&!App::menu;}
    bool ShouldPop()const{return popped;}
    void OpenUsbRepair();
};
// STARTUP_USB_PRODUCTION
void Verify(){
    Case();Reset();Menu manual;
    assert(!startup_starts&&!startup_takes&&!App::option&&!App::progress&&!mutations);
    manual.OpenUsbRepair();assert(nexusapp::guided==1&&!App::progress&&!scans&&!mutations);
    for(unsigned blocked=0;blocked<3;++blocked){Case();Reset();Menu manual;if(blocked==0)manual.focused=false;if(blocked==1)manual.popped=true;if(blocked==2)nexus::lifecycle::poll_allowed=false;manual.OpenUsbRepair();assert(!App::progress&&!startup_stops&&!mutations);}
}

}
int main(){
    Case();Reset();App::applet=true;DisplayUsbImport();assert(App::option&&!App::progress&&!App::menu&&!pending_checks);App::Press({});
    Case();Reset();DisplayUsbImport();App::Run(true);assert(!pending_checks&&!scans&&!mutations&&App::option&&!App::menu);assert(Contains(App::option->message,"No library changes"));
    Case();Reset();pending_rc=Fault;Start();assert(App::option&&!App::menu&&!scans&&!mutations);assert(Contains(App::option->message,"interrupted import")&&Contains(App::option->message,Diagnostic(Fault)));
    for(bool failed:{false,true}){Case();Reset();prepared_scan.complete=false;scan_rc=failed?Fault:0;prepared_scan.detail="Read failed at a specific USB file";Start();assert(!App::menu&&!imports&&!mutations&&App::option);assert(Contains(App::option->message,prepared_scan.detail));if(failed)assert(Contains(App::option->message,Diagnostic(Fault)));}
    Case();Reset();Scan();assert(App::progress_pushes==1&&App::menu->GetShortTitle()==std::string("USB games"));
    auto screen=App::Draw();assert(Contains(screen,"1 items to verify")&&Contains(screen,"Games: 1")&&Contains(screen,"Updates: 0")&&Contains(screen,"DLC items: 0"));
    assert(Contains(screen,"Need checking:")&&Contains(screen,"Games: 1"));
    assert(App::Has("Repair ready games")&&App::Has("Prepare USB"));Details("Native registrations are kept");
    const auto counts=CountPreview(prepared_scan,PlanUsbLibrary(prepared_scan));
    for(const auto index:counts.order){Case();App::Key(UiButton::DOWN);App::Key(UiButton::A);const auto& row=prepared_scan.titles[index];
        assert(App::option&&Contains(App::option->message,row.name)&&Contains(App::option->message,row.reason));
        assert(Contains(App::option->message,FriendlyStatus(row.status,counts.selected[index]))&&Contains(App::option->message,"Title ID:")&&Contains(App::option->message,row.meta_path));
        assert(App::option->right.empty());App::Press({});assert(!imports&&!mutations);}
    // Superseded Ready items are browsable but never offered for registration.
    for(auto latest:{UsbImportStatus::Incomplete,UsbImportStatus::TicketMissing,UsbImportStatus::AlreadyRegistered}){Case();Reset();auto row=prepared_scan.titles.front();row.record.key.version=1;prepared_scan.titles={row};row.record.key.version=2;row.status=latest;prepared_scan.titles.push_back(row);Scan();
        assert(!App::Has("Repair ready games")&&App::Has("Prepare USB"));assert(Contains(App::Draw(),"0 items to verify"));Details("No complete game family is ready to repair");App::Key(UiButton::DOWN);App::Key(UiButton::DOWN);Details("A newer version or a native registration can take priority");}
    for(bool content:{false,true}){Case();Reset();prepared_scan.titles.clear();prepared_scan.nca_files=content?3:0;prepared_scan.unreadable_ncas=content?1:0;Scan();assert(!App::Has("Repair ready games"));App::Key(UiButton::A);
        assert(Contains(App::option->message,content?"not necessarily empty":"No installed USB games"));assert(Contains(App::option->message,"NSP, NSZ, XCI or XCZ")&&Contains(App::option->message,prepared_scan.detail));App::Press({});}
    // A controller action can push a modal before the touch branch in the same frame.
    Case();Reset();Scan();auto dialogs=App::option_pushes;App::Frame({UiButton::X,UiButton::Y,UiButton::A},{true,1000,150});assert(App::option_pushes==dialogs+1&&App::option->right=="Repair games");
    App::Frame({UiButton::X,UiButton::Y,UiButton::A},{true,1000,150});assert(App::option_pushes==dialogs+1&&!mutations);App::Press(0);assert(!App::menu->ShouldPop()&&App::menu->HasFocus());
    Case();Reset();Scan();dialogs=App::option_pushes;App::Frame({UiButton::B,UiButton::X},{true,1000,150});assert(App::menu->ShouldPop()&&!App::option&&App::option_pushes==dialogs&&!mutations);
    Case();Reset();Scan();App::Frame({},{true,1000,150});assert(App::option&&App::option->right=="Repair games");App::Press(0);
    for(auto choice:{std::optional<s64>{},std::optional<s64>{0}}){Case();Reset();Scan();App::Select("Repair ready games");assert(App::option->selected==0&&Contains(App::option->message,"backs up")&&Contains(App::option->message,"remain installed after a rollback"));CancelOnce(choice);assert(!imports);App::Select("Repair ready games");assert(App::option->right=="Repair games");App::Press(0);}
    Case();Reset();Scan();App::Select("Repair ready games");AcceptOnce();assert(App::progress->operation==nexus::safety::Operation::MayWrite);App::Run();assert(imports==1&&mutations==1&&!App::option&&!App::progress&&App::Has("Exit Manager"));
    assert(Contains(App::Draw(),"Added: 1 items")&&Contains(App::Draw(),"Not added: 3 items"));Details(import_result.report_path);
    for(const auto& entry:import_result.entries){App::Key(UiButton::DOWN);Details(entry);}assert(!App::exited);App::Select("Exit Manager");assert(App::exited);
    Case();Reset();Scan();App::Select("Repair ready games");App::Press(1);App::Run(true);assert(imports==1&&!mutations&&!App::Has("Exit Manager"));Details("Cancelled before registration");App::Key(UiButton::B);assert(App::menu->ShouldPop()&&!App::exited);
    Case();Reset();Scan();App::Select("Repair ready games");App::Press(1);auto worker=std::move(App::progress);worker->done(Fault);assert(!imports&&!mutations);Details("could not finish");Details(Diagnostic(Fault));
    Case();Reset();for(auto& item:prepared_scan.titles)item.status=UsbImportStatus::AlreadyRegistered;Scan();assert(!App::Has("Repair ready games")&&App::Has("Prepare USB")&&!imports&&!mutations);
    // Pending recovery blocks scanning and preparation, and restoration has its own confirmation.
    for(auto choice:{std::optional<s64>{},std::optional<s64>{0}}){Case();Reset();pending_flag=true;Start();assert(App::menu&&App::Has("Restore backup")&&!App::Has("Prepare USB")&&!scans&&!restores&&!mutations);Details("recovery journal");App::Select("Restore backup");assert(App::option->selected==0);CancelOnce(choice);assert(!restores&&!plans);App::Select("Restore backup");assert(App::option->right=="Restore backup");App::Press(0);}
    Case();Reset();pending_flag=true;Start();App::Select("Restore backup");AcceptOnce();App::Run();assert(restores==1&&mutations==1&&!scans&&!imports&&!plans);Details(restore_result.report_path);App::Select("Exit Manager");assert(App::exited);
    Case();Reset();pending_flag=true;Start();restore_rc=Fault;restore_result.pending=true;restore_result.restart=false;restore_result.detail="The backup could not be read";App::Select("Restore backup");App::Press(1);App::Run();assert(App::Has("Restore backup")&&!App::Has("Exit Manager"));Details(restore_result.detail);Details(Diagnostic(Fault));
    for(bool restart:{false,true})for(Result rc:{Result{0},Fault}){Case();Reset();auto result=std::make_shared<UsbImportResult>();result->restart=restart;result->detail="Outcome";ResultPage(result,rc);assert(App::Has("Exit Manager")==restart);Details("Outcome");if(rc)Details(Diagnostic(rc));App::Key(UiButton::B);assert(App::menu->ShouldPop()&&!App::exited);}
    Case();Reset();auto result=std::make_shared<UsbImportResult>(import_result);result->rolled_back=true;ResultPage(result,Fault);Details("Earlier completed imports are kept");Details("remain installed after a rollback");
    // Long reports stay available in full, and paging exposes every outcome.
    Case();Reset();result=std::make_shared<UsbImportResult>();for(unsigned i=0;i<12;++i)result->entries.push_back("Outcome "+std::to_string(i)+"\n"+std::string(4000,'x')+"\nFinal diagnostic "+std::to_string(i));ResultPage(result,0);
    App::Key(UiButton::RIGHT);Details(result->entries[4]);App::Key(UiButton::RIGHT);Details(result->entries[9]);
    for(unsigned i=0;i<4;++i)App::Key(UiButton::DOWN);Details(result->entries[11]);assert(Contains(App::menu->subheading,"13 / 13"));
    App::Key(UiButton::LEFT);Details(result->entries[6]);App::Key(UiButton::UP);Details(result->entries[5]);
    Case();Reset();Scan();App::Frame({},{true,500,329});assert(!App::option);App::Frame({},{true,500,329});assert(App::option&&Contains(App::option->message,prepared_scan.titles[counts.order[0]].reason));App::Press({});
    // Preparing permissions reuses the completed scan, with read-only planning before consent.
    Case();Reset();BeginPlan();auto* old=App::menu.get();assert(!old->HasFocus());App::Frame({UiButton::X,UiButton::Y});assert(!App::option&&!mutations);App::Run();assert(plans==1&&scans==1&&!prepares&&!mutations&&App::Has("Save preparation"));Details("does not copy games or saves");
    for(const auto& item:prepare_plan.titles){App::Key(UiButton::DOWN);App::Key(UiButton::A);assert(Contains(App::option->message,item.reason)&&Contains(App::option->message,item.identity));App::Press({});}
    Case();Reset();Scan();App::Select("Prepare USB");auto check_again=App::option->callback;App::Press(1);check_again(1);assert(App::progress_pushes==2&&!plans&&!mutations);App::Run();assert(plans==1);
    for(auto choice:{std::optional<s64>{},std::optional<s64>{0}}){Case();Reset();Scan();App::Select("Prepare USB");CancelOnce(choice);assert(!plans&&!prepares);App::Select("Prepare USB");assert(App::option->right=="Check preparation");App::Press(0);}
    for(auto choice:{std::optional<s64>{},std::optional<s64>{0}}){Case();Reset();Plan();App::Select("Save preparation");assert(App::option->right=="Save on USB"&&App::option->selected==0);CancelOnce(choice);assert(!prepares);App::Select("Save preparation");assert(App::option->right=="Save on USB");App::Press(0);}
    Case();Reset();Plan();App::Select("Save preparation");AcceptOnce();assert(App::progress->operation==nexus::safety::Operation::MayWrite);App::Run();assert(prepares==1&&mutations==1&&!imports&&!restores);Details(prepare_result.report_path);Details("original backup");for(const auto& entry:prepare_result.entries){App::Key(UiButton::DOWN);Details(entry);}App::Key(UiButton::B);assert(App::menu->ShouldPop()&&!App::exited);
    Case();Reset();Plan();App::Select("Save preparation");App::Press(1);App::Run(true);assert(!mutations&&Contains(App::Draw(),"Preparation stopped"));Details("Cancelled before saving");
    Case();Reset();prepare_plan.tickets_to_save=0;Plan();assert(!App::Has("Save preparation")&&!prepares&&!mutations);Details("Preparation copies only");
    for(bool failed:{false,true}){Case();Reset();prepare_plan.complete=false;plan_rc=failed?Fault:0;prepare_plan.detail="Permission check failed before saving";Plan();assert(!prepares&&!mutations&&App::option&&Contains(App::option->message,prepare_plan.detail));if(failed)assert(Contains(App::option->message,Diagnostic(Fault)));}
    Case();Reset();BeginPlan();App::Run(true);assert(plans==1&&!prepares&&!mutations&&App::option&&Contains(App::option->message,"Cancelled"));
    Case();Reset();Plan();App::Select("Save preparation");App::Press(1);worker=std::move(App::progress);worker->done(Fault);assert(!prepares&&!mutations);Details("Preparation could not finish");Details(Diagnostic(Fault));
    // Selection comes from the backend family plan, including healthy USB
    // siblings. The UI must not reconstruct a Ready-only or per-item plan.
    Case();Reset();auto base=prepared_scan.titles.front();auto healthy=base;healthy.record.key.id+=0x800;healthy.record.key.type=NcmContentMetaType_Patch;healthy.status=UsbImportStatus::AlreadyRegistered;healthy.name="Healthy update";prepared_scan.titles={base,healthy};
    injected_plan=UsbLibraryPlan{{1,1,0,0,2},{true,true},{"",""}};Scan();assert(Contains(App::Draw(),"Games ready: 1")&&Contains(App::Draw(),"2 items to verify")&&App::Has("Repair ready games"));
    App::Key(UiButton::DOWN);App::Key(UiButton::DOWN);Details("already healthy");Details("Included in family repair");App::Select("Repair ready games");assert(Contains(App::option->message,"Repair 1 game?")&&Contains(App::option->message,"Native registrations are kept"));App::Press(0);assert(!mutations);
    Case();Reset();prepared_scan.titles={base,healthy};injected_plan=UsbLibraryPlan{{1,0,1,1,0},{false,false},{"Latest DLC permissions are missing","Latest DLC permissions are missing"}};Scan();assert(!App::Has("Repair ready games")&&Contains(App::Draw(),"0 items to verify"));App::Key(UiButton::DOWN);Details("Latest DLC permissions are missing");Details("whole game family must pass");assert(!mutations);
    Case();Reset();injected_plan=UsbLibraryPlan{{1,1,0,0,1},{},{}};Scan();assert(!App::Has("Repair ready games")&&Contains(App::Draw(),"0 items to verify")&&!mutations);
    librarycheck::Verify();networkcheck::Verify();startupcheck::Verify();
    std::cout<<"Guided USB import actual production UI: "<<cases<<" flow cases passed; scan/preview/confirm/result, direct report details, cancellation/recovery, duplicate-input guards, preparation, installed-library entry and network save/session regression passed\n";
}
