#include <functional>
#include <memory>
#include <optional>
#include <fstream>
constexpr Result Result_TransferCancelled=456;
namespace nexus::safety {enum class Operation{ReadOnly,MayWrite};}
namespace sphaira::ui::menu::nexusmenu {
enum class ProgressBoxOption{None};
struct OptionBox {
    using Callback=std::function<void(std::optional<s64>)>;
    std::string message,left,right;int selected{};Callback cb;
    OptionBox(std::string m,std::string a,Callback c):message(m),left(a),cb(c){}
    OptionBox(std::string m,std::string a,std::string b,int i,Callback c):message(m),left(a),right(b),selected(i),cb(c){}
};
struct ProgressBox: sphaira::ui::ProgressBox {
    static inline int runs{};
    ProgressBox(int,const std::string&,const std::string&,std::function<Result(ProgressBox*)> work,
        std::function<void(Result)> done,ProgressBoxOption=ProgressBoxOption::None,
        nexus::safety::Operation op=nexus::safety::Operation::MayWrite){
        assert(op==nexus::safety::Operation::MayWrite);++runs;done(work(this));
    }
};
struct App {
    static inline std::unique_ptr<OptionBox> current;
    static inline bool exited{};
    static bool IsApplet(){return false;}
    template<class T,class... Args>static void Push(Args&&... args){
        if constexpr(std::is_same_v<T,OptionBox>)current=std::make_unique<T>(std::forward<Args>(args)...);
        else T worker(std::forward<Args>(args)...);
    }
    static void Exit(){exited=true;}
    static void Press(std::optional<s64> choice){auto cb=current->cb;current.reset();cb(choice);}
};
struct Menu {
    void RecoverUsbIndexes(u64 only_application=0);
    void Report(Result,const std::string& m){App::Push<OptionBox>(m,"Done",[](auto){});}
};
// RECOVERY_UI_PRODUCTION
// RECOVERY_WORKFLOW_PRODUCTION
}
constexpr u64 A=0x0100123400000000ULL;
void AddGame(){
    Record r{{A,0,0x80,0,{}},{}};NcmContentMetaHeader h{0,3,0,0,0};
    r.data.resize(sizeof(h)+3*sizeof(NcmContentInfo));memcpy(r.data.data(),&h,sizeof(h));
    for(u8 i=0;i<3;++i){NcmContentInfo info{};info.content_id.c[0]=i+1;info.size_low=0x1000;info.content_type=i==2?3:i;
        memcpy(r.data.data()+sizeof(h)+i*sizeof(info),&info,sizeof(info));
        usb_disk[NcaPath(i+1)]=Bytes(0x1000);usb_disk[NcaPath(i+1)][0]=i+1;
    }
    records.push_back(r);title_names[A]="Example game";
}
int main(int argc,char** argv){
    using namespace sphaira::ui::menu::nexusmenu;using namespace sphaira::nexusapp;
    const auto save=[&](const char* name,const std::string& text){if(argc>1)std::ofstream(std::string(argv[1])+"/"+name+".txt")<<text;};
    auto plan=std::make_shared<RecoveryPlan>();plan->detail="Technical findings";
    // No publication: clean/failed checks can return to My games without exit.
    plan->only_application=A;plan->verification_started=plan->verification_complete=plan->repair_complete=true;plan->report_path="SD:/report.txt";
    ShowRecoveryResult(plan,0);assert(App::current->right=="Done" && App::current->selected==1);
    assert(App::current->message.find("Check complete")!=std::string::npos);save("clean",App::current->message);
    App::Press({});assert(!App::exited && !App::current);
    plan->issues.resize(3);for(auto& i:plan->issues){i.name="Example game";i.nca=std::string(32,'a')+".nca";}
    plan->mismatch_files=3;
    for(bool published:{false,true}){
        plan->published=published;App::exited=false;ShowRecoveryResult(plan,0);
        assert(App::current->message.find("attention")!=std::string::npos);save("issues",App::current->message);
        App::Press(0);assert(App::current->message==plan->detail);App::Press(1);assert(App::current->message.find("1 / 3")!=std::string::npos);
        App::Press(1);assert(App::current->message.find("2 / 3")!=std::string::npos);App::Press(0);assert(App::current->message.find("1 / 3")!=std::string::npos);
        App::Press({});assert(App::current->left=="Details" && !App::exited);App::Press({});assert(App::exited==published);
        App::exited=false;ShowRecoveryResult(plan,Fault);assert(App::current->message.find(published?"Repair did not finish":"Check could not finish")!=std::string::npos);
        save(published?"interrupted":"failed",App::current->message);App::Press(1);assert(App::exited==published);
    }
    RecoveryPlan p;p.intact=12;
    assert(RecoverySummary(p,0).find("not the contents of every game")!=std::string::npos);
    save("library",RecoverySummary(p,0));p.only_application=A;
    assert(RecoverySummary(p,0).find("No repair available")!=std::string::npos);
    p.skipped=1;assert(RecoverySummary(p,0).find("attention")!=std::string::npos);
    p.verification_complete=true;p.repair_complete=true;assert(RecoverySummary(p,0).find("Check complete")==std::string::npos);
    assert(RecoverySummary(p,Result_TransferCancelled,true).find("Check cancelled")!=std::string::npos);
    assert(RecoverySummary(p,nexus::registration::RepairPending).find("earlier library change")!=std::string::npos);
    save("confirm-game",RecoveryConfirmation(true));save("confirm-library",RecoveryConfirmation(false));
    // Exercise the actual menu workflow: Cancel makes no worker or writes;
    // Start performs scan+verification+repair in ONE MayWrite-protected worker.
    Reset();AddGame();Menu menu;App::exited=false;menu.RecoverUsbIndexes(A);App::Press({});assert(ProgressBox::runs==0 && writes==0);
    menu.RecoverUsbIndexes(A);App::Press(1);assert(ProgressBox::runs==1 && reads==3 && commits>0);
    assert(App::current->message.find("Repair complete")!=std::string::npos);save("repaired",App::current->message);
    App::Press({});assert(App::exited);
    // Existing valid library is still fully rehashed for a selected game; no
    // extra confirmation, no forced exit, no claim to reset corruption flags.
    App::exited=false;menu.RecoverUsbIndexes(A);App::Press(1);assert(ProgressBox::runs==2 && reads==6);
    assert(App::current->message.find("Check complete")!=std::string::npos);App::Press(1);assert(!App::exited);
    // Whole-library no-op doesn't rehash intact files or claim it did.
    menu.RecoverUsbIndexes();App::Press(1);assert(reads==6);assert(App::current->message.find("not the contents of every game")!=std::string::npos);App::Press(1);
    // Real pending transaction: do NOT auto-restore on Start. Restore requires
    // a separate opt-in, and per-game calls never restore a whole library.
    nexus::repair::Snapshot before;for(size_t i=0;i<3;++i)before[i]={true,disk.at(nexus::repair::Live(i))};
    nexus::repair::Journal journal{9,before,nexus::repair::Encode({})};disk[nexus::repair::Pending]=nexus::repair::EncodeJournal(journal);dirs.insert(nexus::repair::Stage(journal));
    const auto saved_disk=disk;menu.RecoverUsbIndexes();App::Press(1);
    assert(App::current->right=="Restore backup" && disk==saved_disk);save("restore-confirm",App::current->message);
    App::Press(0);assert(disk==saved_disk);menu.RecoverUsbIndexes(A);App::Press(1);assert(disk==saved_disk);
    assert(App::current->message.find("earlier library change")!=std::string::npos);App::Press(1);
    menu.RecoverUsbIndexes();App::Press(1);App::Press(1);
    assert(App::current->message.find("Backup restored")!=std::string::npos);App::Press({});assert(App::exited && handles==0);
    std::cout<<"PASS R1 production menu, automatic flow, explicit restore, result truthfulness and B/Done/Exit safety\n";
}
