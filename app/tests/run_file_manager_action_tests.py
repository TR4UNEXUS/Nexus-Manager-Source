"""Exercise the actual file browser handlers with host UI/IO boundaries stubbed.

No disk mutations: Paste/Remove are observed at their existing confirmation
entry points. Transfer and deletion IO are covered by run_file_manager_tests.py.
"""
from pathlib import Path
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    out = args.out or app / 'build/file-manager-action-tests'
    out.mkdir(parents=True, exist_ok=True)
    source = (app / 'sphaira/source/ui/menus/manager_tools_menu.cpp').read_text(encoding='utf-8')

    def section(start, end):
        return source[source.index(start):source.index(end, source.index(start))]

    production = section('struct FileControl', 'void Text(')
    production += section('void Menu::SetupActions()', 'size_t Menu::Count()')
    production += section('void Menu::Storage()', 'void Menu::FileActions()')
    harness = r'''
#include "ui/file_manager_layout.hpp"
#include "nexus/file_manager_core.hpp"
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <iostream>
using u64=std::uint64_t;
namespace view=sphaira::ui::fileview;
enum class Button { A,B,X,Y,R2,L,R,SELECT,START };
enum class Mode { Volumes,Files,Saves };
struct Action {
    std::string label;std::function<void()> call;
    explicit Action(std::function<void()> cb):call(std::move(cb)){}
    Action(std::string text,std::function<void()> cb):label(std::move(text)),call(std::move(cb)){}
};
namespace nexusapp {struct Item {std::string name,path;u64 size{};bool directory{};};}
namespace nexusfiles {struct Selection {bool usb;u64 generation;nexusapp::Item item;};}
namespace nexusmanager {std::string Display(const std::string& text){return text;}}
struct OptionBox {};
struct SidebarEntryCallback {};
struct Sidebar {
    enum class Side { RIGHT };
    std::map<std::string,std::function<void()>> entries;
    Sidebar(const char*,const char*,Side){}
    template<class T,class F,class... Rest> void Add(std::string title,F cb,Rest...){entries.emplace(title,cb);}
};
namespace devoptab {void DisplayNetworkLocations(){}}
bool full_memory=true;
bool FullMemory(){return full_memory;}
struct App {
    static inline unsigned messages{};
    static inline std::string last_message,notification;
    static inline std::unique_ptr<Sidebar> sidebar;
    static void Exit(){}
    static void Notify(std::string text){notification=std::move(text);}
    template<class T,class... Args> static void Push(const char* text,Args...){++messages;last_message=text;}
    static void Push(std::unique_ptr<Sidebar> menu){sidebar=std::move(menu);}
};
struct Menu {
    Mode mode=Mode::Files;size_t selected{};bool usb{},cut{},actions_dirty{};u64 generation=7;
    std::string path="/",focus_path;
    std::vector<nexusapp::Item> files={{"demo.bin","/demo.bin",123,false}};
    std::optional<nexusfiles::Selection> clipboard;
    std::map<Button,Action> actions;
    unsigned pastes{},deletion_reviews{},refreshes{},more{};
    nexusapp::Item reviewed;
    void SetAction(Button b,Action a){actions.insert_or_assign(b,std::move(a));}
    void RemoveAction(Button b){actions.erase(b);}
    void Press(Button b){assert(actions.contains(b));actions.at(b).call();}
    void Open(){} void Back(){} void Refresh(){++refreshes;}
    void FileActions(){++more;}
    void Paste(){++pastes;}
    void Remove(const nexusapp::Item& item){++deletion_reviews;reviewed=item;}
    void SetupActions();bool FileActionEnabled(view::FileAction) const;
    void RunFileAction(view::FileAction);void Storage();void ClearClipboard();
    void SelectClipboard(const nexusapp::Item&,bool);
};
PRODUCTION
int main(){
    for(bool source_usb:{false,true})for(bool move:{false,true}){
        Menu m;m.usb=source_usb;m.SetupActions();
        m.Press(move?Button::Y:Button::X);
        assert(m.clipboard && m.cut==move && m.clipboard->usb==source_usb);
        assert(m.clipboard->generation==7 && m.clipboard->item.path=="/demo.bin");
        assert(!m.pastes && !m.deletion_reviews);
        // Actual storage selection changes the destination, never the clipboard.
        m.Press(Button::R);assert(App::sidebar);
        App::sidebar->entries.at(source_usb?"SD card":"USB drive")();
        assert(m.usb!=source_usb && m.files.empty() && m.path=="/" && m.refreshes==1);
        assert(m.clipboard->usb==source_usb && m.clipboard->generation==7 && m.cut==move);
        m.SetupActions();m.Press(Button::L);assert(m.pastes==1);
        // A protected destination cannot paste even with a valid clipboard.
        m.path="/Nintendo/Contents";m.Press(Button::L);assert(m.pastes==1);
        m.ClearClipboard();m.path="/";m.Press(Button::L);assert(m.pastes==1);
        assert(App::last_message.find("Select a file")!=std::string::npos);
    }
    for(const char* path:{"/Nintendo/Contents/a.nca","/registered/a.nca","/switch/nexusmanager/transfers/install/a.nsp"}){
        Menu m;m.files[0].path=path;m.SetupActions();
        for(auto button:{Button::X,Button::Y,Button::R2})m.Press(button);
        assert(!m.clipboard && !m.deletion_reviews && !m.pastes);
    }
    {
        Menu m;m.SetupActions();m.Press(Button::R2);
        assert(m.deletion_reviews==1 && m.reviewed.path=="/demo.bin");
        m.files[0].path="/out/.nexus-fm-0123456789abcdef0123456789abcdef";
        m.Press(Button::X);m.Press(Button::Y);assert(!m.clipboard);
        m.Press(Button::R2);assert(m.deletion_reviews==2); // exact retained stage can be reviewed
        m.files.clear();m.Press(Button::R2);assert(m.deletion_reviews==2);
        m.Press(Button::SELECT);assert(m.more==1);
    }
    {
        Menu m;m.SetupActions();full_memory=false;
        for(auto button:{Button::X,Button::Y,Button::R2,Button::L})m.Press(button);
        assert(!m.clipboard && !m.deletion_reviews && !m.pastes);full_memory=true;
        m.mode=Mode::Saves;m.SetupActions();m.Press(Button::X);assert(m.refreshes==1);
        for(auto button:{Button::Y,Button::R2,Button::L,Button::R,Button::SELECT})assert(!m.actions.contains(button));
        m.mode=Mode::Volumes;m.SetupActions();
        for(auto button:{Button::X,Button::Y,Button::R2,Button::L})assert(!m.actions.contains(button));
    }
    std::cout<<"PASS production file action/controller handlers: SD/USB clipboard pinning, storage changes, empty-folder paste, protected paths, deletion review and mode gates\n";
}
'''
    fixture = out / 'file_manager_actions.cpp'
    fixture.write_text(harness.replace('PRODUCTION', production), encoding='utf-8')
    binary = out / 'file_manager_actions'
    subprocess.run(['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-I' + str(app / 'sphaira/include'),
                    str(fixture), '-o', str(binary)], check=True)
    subprocess.run([str(binary.resolve())], check=True)


if __name__ == '__main__':
    main()
