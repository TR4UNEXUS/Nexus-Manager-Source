#include <functional>
#include <memory>
#include <optional>
namespace sphaira::ui::menu::nexusmenu {
struct OptionBox {
    using Callback=std::function<void(std::optional<s64>)>;
    std::string message,left,right;int selected{};Callback cb;
    OptionBox(std::string m,std::string a,Callback c):message(m),left(a),cb(c){}
    OptionBox(std::string m,std::string a,std::string b,int i,Callback c):message(m),left(a),right(b),selected(i),cb(c){}
};
struct App {
    static inline std::unique_ptr<OptionBox> current;
    static inline bool exited{};
    template<class T,class... Args>static void Push(Args&&... args){current=std::make_unique<T>(std::forward<Args>(args)...);}
    static void Exit(){exited=true;}
    static void Press(std::optional<s64> choice){auto cb=current->cb;current.reset();cb(choice);}
};
// RECOVERY_UI_PRODUCTION
}
int main(){
    using namespace sphaira::ui::menu::nexusmenu;
    auto plan=std::make_shared<sphaira::nexusapp::RecoveryPlan>();plan->detail="Finished";
    ShowRecoveryResult(plan);App::Press({});assert(App::exited); // B cannot reopen stale menu
    plan->issues.resize(3);for(size_t i=0;i<3;++i){plan->issues[i].name="Example";plan->issues[i].nca=std::string(32,'a')+".nca";}
    for(bool published:{false,true}){
        plan->published=published;App::exited=false;
        ShowRecoveryResult(plan);assert(App::current->left=="Review issues");
        App::Press(0);assert(App::current->message.find("1 / 3")!=std::string::npos);
        App::Press(1);assert(App::current->message.find("2 / 3")!=std::string::npos);
        App::Press(0);assert(App::current->message.find("1 / 3")!=std::string::npos);
        App::Press(0);assert(App::current->message=="Finished");
        App::Press(0);App::Press(1);App::Press(1);assert(App::current->message.find("3 / 3")!=std::string::npos);
        App::Press(1);assert(App::current->message=="Finished");
        App::Press(0);App::Press({});assert(App::current->message=="Finished" && !App::exited);
        App::Press({});assert(App::exited);
        App::exited=false;ShowRecoveryResult(plan);App::Press(1);assert(App::exited);
    }
    std::cout<<"PASS: production results UI next/previous/summary, B behavior and exit-only after publication\n";
}
