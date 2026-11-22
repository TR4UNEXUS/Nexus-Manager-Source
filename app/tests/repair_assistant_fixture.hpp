// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "nexus/repair_catalog_core.hpp"
using u8=uint8_t;using u64=uint64_t;using Result=uint32_t;
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
std::string operator""_nx(const char*s,size_t n){return {s,n};}
namespace i18n{std::string get(const std::string&s){return s;}}
namespace nexus{void Log(const char*,Result){}}
namespace nexus::safety{enum class Operation{ReadOnly,MayWrite};}
namespace sphaira::ui{
enum class ProgressBoxOption{None};
struct OptionBox{
 using Callback=std::function<void(std::optional<int>)>;std::string text,left,right;Callback callback;
 OptionBox(std::string t,std::string l,Callback c):text(t),left(l),callback(c){}
 OptionBox(std::string t,std::string l,std::string r,int,Callback c):text(t),left(l),right(r),callback(c){}
};
struct SidebarEntryCallback {std::string label,detail;std::function<void()> callback;SidebarEntryCallback(std::string l,std::function<void()> c,std::string d={}):label(l),detail(d),callback(c){}};
struct Sidebar {enum class Side{RIGHT};std::string title,subtitle;float width;std::vector<SidebarEntryCallback> entries;
 Sidebar(std::string t,std::string s,Side,float w):title(t),subtitle(s),width(w){}
 template<class T,class...Args>void Add(Args&&...args){entries.emplace_back(std::forward<Args>(args)...);}
};
struct ProgressBox{
 std::function<Result(ProgressBox*)> work;std::function<void(Result)> done;nexus::safety::Operation op;bool cancel{};
 ProgressBox(int,std::string,std::string,std::function<Result(ProgressBox*)> w,std::function<void(Result)>d,ProgressBoxOption=ProgressBoxOption::None,nexus::safety::Operation o=nexus::safety::Operation::MayWrite):work(w),done(d),op(o){}
};
}
namespace sphaira{struct App {
 static inline std::unique_ptr<ui::OptionBox> option;static inline std::unique_ptr<ui::ProgressBox> progress;static inline std::unique_ptr<ui::Sidebar> sidebar;static inline bool applet{};static inline unsigned workers{};
 static bool IsApplet(){return applet;}
 template<class T,class...Args>static void Push(Args&&... args){if constexpr(std::is_same_v<T,ui::OptionBox>)option=std::make_unique<T>(std::forward<Args>(args)...);else{progress=std::make_unique<T>(std::forward<Args>(args)...);++workers;}}
 static void Push(std::unique_ptr<ui::Sidebar> p){sidebar=std::move(p);}
 static void Run(bool cancel=false){auto p=std::move(progress);p->cancel=cancel;auto rc=p->work(p.get());p->done(rc);}
 static bool Has(const std::string& name){return sidebar&&std::any_of(sidebar->entries.begin(),sidebar->entries.end(),[&](auto&e){return e.label==name;});}
 static void Select(const std::string& name){assert(Has(name));for(auto e:sidebar->entries)if(e.label==name){e.callback();return;}}
 static void Answer(std::optional<int>a){auto p=std::move(option);p->callback(a);}
};}
namespace nexus::catalog{struct Loaded {Catalog catalog;Bytes bytes;bool cached{},cache_rejected{};};inline unsigned loads{},updates{};inline Result load_rc{};Result Load(Loaded& out){++loads;out.catalog.revision=1;return load_rc;}Result Update(sphaira::ui::ProgressBox*,std::string&){++updates;return 0;}}
namespace nexus::reports{unsigned opens{},stops{};void Stop(){++stops;}void DisplayReportMenu(){++opens;}}
namespace nexus::updates{constexpr char Version[]="0.2.28";unsigned VersionCode(const char*){return 2028;}void StopStartupCheck(){}}
namespace sphaira::nexusapp{
struct RecoveryScanRecord{u64 application{},record{};unsigned version{};std::string name,status;};
struct RecoveryPlan {
 struct Meta{u64 application_id{};struct {u64 id{};unsigned version{},type{0x80};}key;std::string name;std::vector<size_t>files;};
 struct File{bool verified_modified_meta{};struct{std::array<u8,16>content_id;}info;std::array<u8,32>actual_hash;};
 struct Issue{std::string name,kind;};struct Before{bool exists{true};};
 bool ready{true},pending{};std::string detail="Preview",backup_detail,report_text,report_path;
 std::vector<RecoveryScanRecord>scan_records{{1,1,0,"Game","Missing links"}};
 std::vector<Meta>metadata{{1,{1,0,0x80},"Game",{}}};std::vector<File>files;std::vector<Issue>issues;std::array<Before,3>before;
};
Result scan_rc{},apply_rc{};unsigned scans{},applies{},imports{},backups{};u64 last_app{};
Result ScanUsbRecovery(ui::ProgressBox*p,RecoveryPlan& plan,u64 app){++scans;last_app=app;if(scan_rc||p->cancel){plan.ready=false;return scan_rc?scan_rc:123;}return 0;}
Result ApplyUsbRecovery(ui::ProgressBox*p,RecoveryPlan& plan){++applies;plan.ready=false;return p->cancel?123:apply_rc;}
std::string RecoverySummary(const RecoveryPlan&,Result){return "Summary";}std::string RecoveryIssuePage(const RecoveryPlan&,size_t){return "Issue";}
void DisplayUsbImport(){++imports;}void DisplayConsoleRoutingBackup(){++backups;}void DisplayGuidedUsbRepair(u64 application=0);
}
