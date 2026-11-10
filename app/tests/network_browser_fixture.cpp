// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "nexus/network_browser_model.hpp"
#include "ui/nexus_icons.hpp"
struct Vec4{float x,y,w,h;};
struct NVGcontext{};struct NVGcolor{int r,g,b;};struct Theme{};
constexpr int NVG_ROUND=1,NVG_ALIGN_LEFT=1,NVG_ALIGN_TOP=2,NVG_ALIGN_RIGHT=4,NVG_ALIGN_CENTER=8;
NVGcolor nvgRGB(int r,int g,int b){return {r,g,b};}
void nvgSave(NVGcontext*){}void nvgRestore(NVGcontext*){}void nvgBeginPath(NVGcontext*){}
void nvgMoveTo(NVGcontext*,float,float){}void nvgLineTo(NVGcontext*,float,float){}void nvgClosePath(NVGcontext*){}void nvgStroke(NVGcontext*){}
void nvgRoundedRect(NVGcontext*,float,float,float,float,float){}void nvgCircle(NVGcontext*,float,float,float){}
void nvgTranslate(NVGcontext*,float,float){}void nvgScale(NVGcontext*,float,float){}void nvgStrokeColor(NVGcontext*,NVGcolor){}
void nvgStrokeWidth(NVGcontext*,float){}void nvgLineCap(NVGcontext*,int){}void nvgLineJoin(NVGcontext*,int){}void nvgScissor(NVGcontext*,float,float,float,float){}
enum class Button{START,A,B,X,Y,R,LEFT,RIGHT,UP,DOWN};
struct Action{std::string hint;std::function<void()> callback;Action(std::function<void()> cb):callback(std::move(cb)){}Action(std::string h,std::function<void()> cb):hint(std::move(h)),callback(std::move(cb)){}};
struct Controller{std::optional<Button> press;};struct TouchInfo{bool is_clicked{};float x{},y{};bool in_range(Vec4 r)const{return x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h;}};
namespace drawn{struct Text{float x,y,width,size;NVGcolor color;std::string value;};std::vector<Vec4> cards;std::vector<Text> texts;unsigned chrome{};void Reset(){cards.clear();texts.clear();chrome=0;}}
namespace sphaira::ui {
struct ScrollingText{void Draw(NVGcontext*,bool,float x,float y,float w,float size,int,NVGcolor color,const std::string& value){drawn::texts.push_back({x,y,w,size,color,value});}};
namespace gfx{void drawText(NVGcontext*,float x,float y,float size,NVGcolor color,const char* text,int){drawn::texts.push_back({x,y,0,size,color,text});}}
namespace violet{void Card(NVGcontext*,Vec4 r,bool,bool=false){drawn::cards.push_back(r);}}
namespace menu {
constexpr unsigned MenuFlag_None=0;
struct MenuBase{std::map<Button,Action> actions;bool focus{true},pop{};std::string sub;
    MenuBase(const char*,unsigned){}virtual ~MenuBase()=default;virtual const char* GetShortTitle()const=0;
    virtual void Update(Controller* controller,TouchInfo*){if(controller->press){auto cb=actions.at(*controller->press).callback;controller->press.reset();cb();}}
    virtual void Draw(NVGcontext*,Theme*){++drawn::chrome;}
    void SetAction(Button button,Action action){actions.insert_or_assign(button,std::move(action));}void RemoveAction(Button button){actions.erase(button);}void SetPop(){pop=true;}bool ShouldPop()const{return pop;}bool HasFocus()const{return focus;}void SetSubHeading(std::string value){sub=std::move(value);}
};
namespace network {
// BROWSER_CLASS
}
}
}
// BROWSER_SOURCE
using namespace nexus::network::browser;
using sphaira::ui::menu::network::Browser;
void Press(Browser& view,Button button){Controller c{button};TouchInfo t;view.Update(&c,&t);}
std::vector<Row> Rows(size_t count,unsigned& clicked){std::vector<Row> rows;for(size_t i=0;i<count;++i)rows.push_back({std::string(200,'A')+std::to_string(i),"10.00 GiB",i%2?Kind::File:Kind::Folder,[&clicked]{++clicked;}});return rows;}
int main(){
    unsigned clicked{},refreshed{},back{};auto state=std::make_shared<Model>();state->Replace("Install Games / Browse SMB","Office NAS:/",Rows(18,clicked),"Choose a game file","No supported game files",{},[&]{++refreshed;});
    Browser browser(state);assert(browser.actions.at(Button::B).hint=="Back");assert(browser.actions.at(Button::A).hint=="Open folder");
    Press(browser,Button::DOWN);assert(state->selected==1&&browser.actions.at(Button::A).hint=="Select");Press(browser,Button::A);assert(clicked==1);
    Press(browser,Button::RIGHT);assert(state->selected==8&&state->PageStart()==7);Press(browser,Button::RIGHT);Press(browser,Button::RIGHT);assert(state->selected==17);Press(browser,Button::DOWN);assert(state->selected==17);
    Press(browser,Button::LEFT);assert(state->selected==10);Press(browser,Button::X);assert(refreshed==1);
    state->selected=0;Controller controller;TouchInfo select{true,200,174+60+15};browser.Update(&controller,&select);assert(state->selected==1&&clicked==1);browser.Update(&controller,&select);assert(clicked==2);
    drawn::Reset();NVGcontext vg;Theme theme;browser.Draw(&vg,&theme);assert(drawn::chrome==1&&drawn::cards.size()==8);
    assert(drawn::cards[0].x==42&&drawn::cards[0].w==1196); // full-width content panel, not a sidebar
    for(const auto& card:drawn::cards){assert(card.x>=0&&card.y>=100&&card.x+card.w<=1280&&card.y+card.h<=640);}
    size_t filenames{};for(const auto& text:drawn::texts){assert(text.color.r==255&&text.color.g==255&&text.color.b==255);if(text.value.starts_with(std::string(10,'A'))){++filenames;assert(text.x==120&&text.width==850&&text.x+text.width<990);}}
    assert(filenames==7); // only the visible page is rendered, including long-name clipping/scrolling
    state->selected=8;state->Replace("Browse SMB","Office NAS:/folder",Rows(1,clicked),"Choose a game file","Empty",[&]{++back;});Press(browser,Button::B);assert(back==1&&!browser.ShouldPop()&&browser.actions.at(Button::B).hint=="Parent folder");
    state->Replace("Browse SMB","Office NAS:/",Rows(18,clicked),"Choose a game file","Empty");assert(state->selected==8);
    state->Replace("Browse SMB","Office NAS:/",Rows(2,clicked),"Choose a game file","Empty");assert(state->selected==1); // refreshed listing shrank
    state->Replace("Browse NFS","NAS:/empty",{},"Choose another folder","This folder is empty");Press(browser,Button::DOWN);Press(browser,Button::RIGHT);Press(browser,Button::A);assert(state->selected==0&&clicked==2);
    drawn::Reset();browser.Draw(&vg,&theme);assert(std::any_of(drawn::texts.begin(),drawn::texts.end(),[](const auto& text){return text.value=="This folder is empty";}));
    Press(browser,Button::B);assert(browser.ShouldPop());
    for(unsigned i=0;i<200;++i)state->Replace("Browse NFS","NAS:/"+std::to_string(i),{},"","Empty");assert(state->history.size()<=64);
    // Queue selections persist across folders and retain the exact reviewed
    // size until the user explicitly unmarks and selects a changed file.
    auto selection=std::make_shared<Model>();std::vector<Row> selectable;
    for(size_t i=0;i<70;++i)selectable.push_back({"Package "+std::to_string(i),"1 MiB",Kind::File,[&]{++clicked;},"/folder/game"+std::to_string(i)+".nsp",100+i});
    selection->Replace("Browse SMB","NAS:/folder",selectable,"Mark files","Empty",{},[&]{++refreshed;});
    std::vector<SelectedFile> queued;selection->SetSelectionAction([&](auto files){queued=std::move(files);});Browser queue_browser(selection);
    assert(queue_browser.actions.at(Button::X).hint=="Install selected"&&queue_browser.actions.at(Button::Y).hint=="Mark file");
    Press(queue_browser,Button::Y);assert(selection->marked.size()==1&&queue_browser.actions.at(Button::Y).hint=="Unmark");
    Press(queue_browser,Button::DOWN);Press(queue_browser,Button::Y);Press(queue_browser,Button::X);
    assert(queued.size()==2&&queued[0].path=="/folder/game0.nsp"&&queued[1].path=="/folder/game1.nsp");
    Press(queue_browser,Button::R);assert(refreshed==2);
    selection->Replace("Browse SMB","NAS:/different",{{"Changed package","2 MiB",Kind::File,[]{},"/folder/game0.nsp",999}},"","Empty");
    assert(selection->marked.size()==2&&selection->marked[0].size==100&&selection->IsMarked(selection->rows[0]));
    Press(queue_browser,Button::Y);Press(queue_browser,Button::Y);assert(selection->marked.back().size==999);
    selection->RemoveSelected({"/folder/game1.nsp"});assert(selection->marked.size()==1&&selection->marked[0].path=="/folder/game0.nsp");
    drawn::Reset();queue_browser.Draw(&vg,&theme);assert(std::any_of(drawn::texts.begin(),drawn::texts.end(),[](const auto& text){return text.value=="1 selected";}));
    for(const auto& text:drawn::texts)if(text.value=="Changed package")assert(text.x==156&&text.width==814&&text.x+text.width==970);
    // Tapping the checkbox changes selection, never opening the item menu.
    const auto activated=clicked;TouchInfo checkbox{true,126,200};queue_browser.Update(&controller,&checkbox);assert(selection->marked.empty()&&clicked==activated);
    queue_browser.Update(&controller,&checkbox);assert(selection->marked.size()==1&&clicked==activated);
    selection->marked.clear();selection->Replace("Browse SMB","NAS:/folder",selectable,"","Empty");
    for(size_t i=0;i<70;++i){selection->selected=i;const auto changed=selection->Toggle();assert(changed==(i<MaxSelection));}
    TouchInfo idle;assert(selection->marked.size()==MaxSelection);selection->selected=69;queue_browser.Update(&controller,&idle);
    assert(queue_browser.actions.at(Button::Y).hint=="64 file limit");
    selection->SetSelectionAction({});queue_browser.Update(&controller,&idle);assert(selection->marked.empty()&&!queue_browser.actions.contains(Button::Y)&&!queue_browser.actions.contains(Button::R)&&queue_browser.actions.at(Button::X).hint=="Refresh");
    std::cout<<"Full-screen production browser: controller/touch navigation, page bounds, refresh/back, white full-width drawing, long filenames, empty folders and retained selection passed\n";
    std::cout<<"Network browser queue selection: Switch actions, touch checkboxes, cross-folder reviewed sizes, completion removal, 64-file cap and no stale file-manager actions passed\n";
}
