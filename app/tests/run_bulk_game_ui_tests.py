"""Exercise production selection, storage picker and single-confirmation bulk deletion."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1];s=(A/'sphaira/source/ui/menus/nexus_menu.cpp').read_text()
def block(name):
 a=s.index(name);b=s.index('{',a);level=1;e=b+1
 while level:level+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
methods='\n'.join(block(n) for n in ('void Menu::ToggleGame(', 'void Menu::DeleteSelectedGames(', 'void Menu::ChooseInstallMode('))
code=r'''
#include "nexus/localize.hpp"
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
using Result=unsigned;using u64=std::uint64_t;
#define R_FAILED(x) ((x)!=0)
namespace nexus {enum class Target{Sd,Internal,Usb};constexpr Result Invalid=123,Unavailable=124;const char* TargetName(Target t){return t==Target::Sd?"SD":t==Target::Internal?"Internal":"USB";}namespace batch {struct Entry{};}}
using nexus::Target;
struct ProgressBox {
 std::function<Result(ProgressBox*)> work;std::function<void(Result)> done;
 ProgressBox(int,const std::string&,const std::string&,decltype(work) w,decltype(done) d):work(w),done(d){}
};
struct ProgressScope{ProgressScope(ProgressBox*,unsigned,unsigned){}};
struct OptionBox{
 std::string text;std::function<void(std::optional<int>)> choose;
 OptionBox(std::string t,const char*,const char*,int,decltype(choose) c):text(t),choose(c){}
 OptionBox(std::string t,const char*,decltype(choose) c):text(t),choose(c){}
};
struct SidebarEntryCallback{};
struct Sidebar {
 enum class Side{RIGHT};std::vector<std::pair<std::string,std::function<void()>>> rows;
 Sidebar(const std::string&,const std::string&,Side,float){}
 template<class T,class F,class... Args>void Add(std::string name,F f,Args&&...){rows.push_back({name,f});}
};
struct App{
 static inline bool applet{};static inline std::unique_ptr<Sidebar> side;static inline std::unique_ptr<OptionBox> option;static inline std::unique_ptr<ProgressBox> progress;
 static bool IsApplet(){return applet;}
 static void Push(std::unique_ptr<Sidebar> p){side=std::move(p);}
 template<class T,class... Args>static void Push(Args&&... a){auto p=std::make_unique<T>(std::forward<Args>(a)...);if constexpr(std::is_same_v<T,ProgressBox>)progress=std::move(p);else option=std::move(p);}
 static void Select(unsigned row){auto f=side->rows.at(row).second;side.reset();f();}
 static void Press(int answer){auto f=option->choose;option.reset();f(answer);}
 static void Run(){auto p=std::move(progress);p->done(p->work(p.get()));}
};
namespace nexusapp {
 struct Game{u64 id;std::string name,homebrew_path;};enum class CleanupKind{Game};
 struct CleanupItem{CleanupKind kind;Target target;u64 application;std::string name;};
 struct CleanupBatchPlan{unsigned ready{};};struct CleanupBatchReport{std::string detail;std::vector<u64> removed_games;};
 unsigned checks{},deletes{};Result check_result{},apply_result{};std::vector<CleanupItem> observed;
 Result PreviewCleanupBatch(ProgressBox*,const std::vector<CleanupItem>& items,CleanupBatchPlan& b){++checks;observed=items;b.ready=items.size();return check_result;}
 Result ApplyCleanupBatch(ProgressBox*,const CleanupBatchPlan&,CleanupBatchReport& r){++deletes;r.detail="done";r.removed_games={observed.front().application};return apply_result;}
}
struct Menu {
 size_t selected{};std::vector<nexusapp::Game> games{{1,"First",{}},{2,"Second",{}},{3,"Port","/switch/port.nro"}};std::set<u64> selected_games;
 bool actions_dirty{},refresh_pending{},reuse{};Target started{Target::Sd};unsigned starts{};Result error{};
 void Report(Result r,const std::string&){error=r;}void GetPolledData(bool){}
 void ToggleGame();void DeleteSelectedGames();void ChooseInstallMode(Target,const std::vector<nexus::batch::Entry>&);
 void StartBatch(Target t,const std::vector<nexus::batch::Entry>&,bool only=false){started=t;reuse=only;++starts;}
};
METHODS
int main(){
 for(auto target:{Target::Sd,Target::Internal,Target::Usb}){
  Menu m;nexusapp::checks=nexusapp::deletes=0;m.ToggleGame();m.selected=1;m.ToggleGame();m.selected=2;m.ToggleGame();
  assert(m.selected_games==std::set<u64>({1,2}));
  m.DeleteSelectedGames();assert(App::side->rows.size()==3&&!nexusapp::checks&&!nexusapp::deletes);
  App::Select(static_cast<unsigned>(target));assert(App::option->text.find("Saved progress")!=std::string::npos);
  App::Press(0);assert(!nexusapp::checks&&!nexusapp::deletes&&!App::progress);
  m.DeleteSelectedGames();App::Select(static_cast<unsigned>(target));App::Press(1);assert(App::progress&&!nexusapp::deletes);
  App::Run();assert(nexusapp::checks==1&&nexusapp::deletes==1&&nexusapp::observed.size()==2);
  for(const auto& item:nexusapp::observed)assert(item.target==target&&(item.application==1||item.application==2));
  assert(m.refresh_pending&&m.selected_games==std::set<u64>{2}); // skipped/failed selection remains
  App::option.reset();
 }
 Menu m;m.ToggleGame();m.ToggleGame();assert(m.selected_games.empty());
 m.ToggleGame();nexusapp::deletes=0;nexusapp::check_result=70;
 m.DeleteSelectedGames();App::Select(2);App::Press(1);App::Run();assert(m.error==70&&!nexusapp::deletes&&m.selected_games.contains(1));
 nexusapp::check_result=0;App::applet=true;App::side.reset();m.DeleteSelectedGames();assert(m.error==nexus::Unavailable&&!App::side);App::applet=false;
 m.ChooseInstallMode(Target::Sd,{});assert(m.starts==1&&!m.reuse);
 m.ChooseInstallMode(Target::Usb,{});assert(App::side->rows.size()==2&&m.starts==1);App::Select(1);assert(m.reuse&&m.started==Target::Usb);
 puts("Bulk game UI passed: exact selection, deselection, SD/internal/USB, one confirmation, cancellation, failure retention, saves warning and link-only picker.");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-bulk-ui-') as d:
 p=Path(d);(p/'actual.cpp').write_text(code.replace('METHODS',methods))
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(A/'sphaira/include'),str(p/'actual.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
