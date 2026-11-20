"""Exercise the production port icon worker/cache with counting storage and GPU mocks."""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
source=(app/'sphaira/source/ui/menus/port_manager_menu.cpp').read_text()
header=(app/'sphaira/include/ui/menus/port_manager_menu.hpp').read_text()
def block(name):
    start=source.index(name);brace=source.index('{',start);depth,end=1,brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
methods='\n'.join(block(n) for n in ('std::string IconKey(', 'void Menu::ReadIconWorker(', 'void Menu::DropIcon(', 'int Menu::CachedImage(', 'void Menu::StoreIcon(', 'void Menu::StopIcon(', 'void Menu::ClearIcons(', 'void Menu::PollIcons('))
fields=header[header.index('    struct IconJob'):header.index('    static void ReadIconWorker')]
fixture=r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "ui/port_library_layout.hpp"
using u8=std::uint8_t;using u64=std::uint64_t;using Result=unsigned;
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
namespace nexusports {
struct Port {std::string name,nro;bool usb{};u64 generation{},application{};};
std::map<std::string,unsigned> reads;std::set<std::string> failed;
Result ReadIcon(const Port& p,std::vector<u8>& bytes){++reads[p.nro];if(failed.contains(p.nro))return 1;bytes={1};return 0;}
}
std::set<int> live;unsigned decodes{},deleted{};int next_image{};
struct App {static void* GetVg(){return nullptr;}};
int MakeIcon(const std::vector<u8>& bytes){++decodes;if(bytes.empty()||!bytes[0])return 0;live.insert(++next_image);return next_image;}
void nvgDeleteImage(void*,int image){assert(image>0&&live.erase(image)==1);++deleted;}
struct Thread {void(*entry)(void*){};void* arg{};bool pending{};};
bool create_failure{},start_failure{};
namespace utils {Result CreateThread(Thread* t,void(*entry)(void*),void* arg,size_t){if(create_failure)return 1;t->entry=entry;t->arg=arg;return 0;}}
Result threadStart(Thread* t){if(start_failure)return 1;t->pending=true;return 0;}
void threadWaitForExit(Thread* t){if(t->pending){t->pending=false;t->entry(t->arg);}}
void threadClose(Thread* t){assert(!t->pending);}
namespace title {
enum class NacpLoadStatus {Loading,Loaded};
struct Title {NacpLoadStatus status{NacpLoadStatus::Loaded};std::vector<u8> icon{1};};
std::map<u64,Title> values;unsigned requests{};
void PushAsync(u64){++requests;}
const Title* GetAsync(u64 id){auto i=values.find(id);return i==values.end()?nullptr:&i->second;}
}
struct Menu {
    size_t selected{};bool title_ready{};
    std::vector<nexusports::Port> ports;
FIELDS
    static void ReadIconWorker(void*);void StopIcon(bool keep=true);void PollIcons();void ClearIcons();
    void StoreIcon(const std::string&,int);void DropIcon(const std::string&);int CachedImage(const std::string&);
};
METHODS
void Populate(Menu& m,unsigned count){
    for(unsigned n=0;n<count;++n)m.ports.push_back({"Port "+std::to_string(n),"/switch/port"+std::to_string(n)+"/app.nro",bool(n%2),1,0});
}
void FillPage(Menu& m){
    for(unsigned n=0;n<40;++n){m.PollIcons();if(m.icon_job)threadWaitForExit(&m.icon_thread);}
    assert(!m.icon_job);
    const auto start=nexus::portgrid::Start(m.selected);
    for(size_t n=start;n<std::min(start+12,m.ports.size());++n)assert(m.icons.contains(IconKey(m.ports[n])));
    assert(m.icons.size()<=m.IconCacheLimit&&live.size()<=m.IconCacheLimit);
}
unsigned Reads(){unsigned result{};for(auto [name,count]:nexusports::reads)result+=count;return result;}
void Reset(){assert(live.empty());nexusports::reads.clear();nexusports::failed.clear();decodes=deleted=0;create_failure=start_failure=false;title::values.clear();title::requests=0;}
int main(){
    {
        Reset();Menu m;Populate(m,36);std::map<std::string,int> handles;
        for(unsigned page=0;page<3;++page){m.selected=page*12;FillPage(m);}
        for(const auto& p:m.ports)handles[IconKey(p)]=m.CachedImage(IconKey(p));
        const auto before=Reads(),made=decodes;
        for(unsigned repeat=0;repeat<100;++repeat)for(unsigned page:{2U,1U,0U}){m.selected=page*12;FillPage(m);}
        assert(Reads()==before&&before==36&&decodes==made&&!deleted);
        for(const auto& p:m.ports)assert(handles.at(IconKey(p))==m.CachedImage(IconKey(p)));
        // Focus loss retains a finished icon; clearing releases every texture.
        m.StopIcon();assert(Reads()==before);m.ClearIcons();assert(live.empty()&&m.icons.empty());
    }
    {
        Reset();Menu m;Populate(m,24);m.PollIcons();const auto first=m.icon_job->key;
        m.selected=12;threadWaitForExit(&m.icon_thread);m.PollIcons();
        assert(m.CachedImage(first)>0);m.StopIcon();m.selected=0;FillPage(m);
        assert(nexusports::reads[m.ports[0].nro]==1);m.ClearIcons();
    }
    {
        Reset();Menu m;Populate(m,144);
        for(unsigned page=0;page<12;++page){m.selected=page*12;FillPage(m);}
        assert(m.icons.size()==96&&live.size()==96&&deleted==48);
        const auto count=Reads();m.selected=120;FillPage(m);m.selected=132;FillPage(m);assert(Reads()==count);
        m.selected=0;FillPage(m);assert(Reads()==count+12);m.ClearIcons();assert(live.empty());
    }
    {
        Reset();Menu m;Populate(m,1);nexusports::failed.insert(m.ports[0].nro);FillPage(m);
        FillPage(m);assert(Reads()==1&&m.CachedImage(IconKey(m.ports[0]))==0);
        nexusports::failed.clear();m.ClearIcons();FillPage(m);assert(Reads()==2);
        // A reconnected USB generation and an SD copy cannot share stale art.
        const auto old=IconKey(m.ports[0]);m.ports[0].usb=true;++m.ports[0].generation;
        FillPage(m);assert(Reads()==3&&m.CachedImage(old)!=m.CachedImage(IconKey(m.ports[0])));
        m.DropIcon(IconKey(m.ports[0]));FillPage(m);assert(Reads()==4);m.ClearIcons();
    }
    for(bool start:{false,true}){
        Reset();Menu m;Populate(m,1);create_failure=!start;start_failure=start;FillPage(m);assert(Reads()==0);
        create_failure=start_failure=false;m.ClearIcons();FillPage(m);assert(Reads()==1);m.ClearIcons();
    }
    {
        Reset();Menu m;Populate(m,1);m.title_ready=true;m.ports[0].application=7;title::values[7]={};
        FillPage(m);FillPage(m);assert(Reads()==0&&decodes==1&&title::requests==1);m.ClearIcons();
    }
    {
        Reset();Menu m;Populate(m,1);m.title_ready=true;m.ports[0].application=8;
        title::values[8]={title::NacpLoadStatus::Loading,{1}};nexusports::failed.insert(m.ports[0].nro);
        FillPage(m);assert(Reads()==1&&m.CachedImage(IconKey(m.ports[0]))==0);
        title::values[8].status=title::NacpLoadStatus::Loaded;FillPage(m);
        assert(Reads()==1&&m.CachedImage(IconKey(m.ports[0]))>0);m.ClearIcons();
    }
    {
        Reset();Menu m;Populate(m,1);m.title_ready=true;m.ports[0].application=9;
        title::values[9]={title::NacpLoadStatus::Loaded,{0}};nexusports::failed.insert(m.ports[0].nro);
        FillPage(m);const auto count=decodes;FillPage(m);
        assert(Reads()==1&&decodes==count&&m.CachedImage(IconKey(m.ports[0]))==0);m.ClearIcons();
    }
    {
        Reset();Menu m;Populate(m,1);m.PollIcons();m.ClearIcons();
        assert(live.empty()&&m.icons.empty()&&!m.icon_job&&!m.icon_thread_started);
    }
    std::cout<<"Production port icons: repeated page navigation reuses textures without reads/decodes; late jobs, focus loss, 96-entry eviction, missing icons, refresh, media identity, HOME icons and thread failures passed\n";
}
""".replace('FIELDS',fields).replace('METHODS',methods)
# UI navigation keeps the cache, while explicit refresh invalidates it.
assert 'ClearIcons()' not in block('void Menu::Refresh()')
assert 'ClearIcons();Refresh();' in block('void Menu::Setup()')
assert 'StopIcon();' in block('void Menu::OnFocusLost()')
assert 'CachedImage(IconKey(item))' in block('void Menu::Draw(')
with tempfile.TemporaryDirectory(prefix='nexus-port-icons-') as temp:
    cpp=Path(temp)/'icons.cpp';cpp.write_text(fixture)
    for opt in ('-O0','-O2'):
        exe=Path(temp)/('icons'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I',str(app/'sphaira/include'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
