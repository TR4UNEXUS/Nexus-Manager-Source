// SPDX-License-Identifier: GPL-3.0-or-later
// The runner inserts the production ReviewMenu and DrawLine, not a copy.
#include "ui/usb_import_view.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
struct Vec4 {float x,y,w,h;};
struct NVGcolor {float r=1,g=1,b=1,a=1;};
struct NVGcontext {};
struct Theme {};
enum class Button {A,B,X,Y,UP,DOWN,LEFT,RIGHT};
struct Controller {std::optional<Button> pressed;bool duplicate{};};
struct TouchInfo {bool is_clicked{};float x{},y{};bool in_range(Vec4 r){return x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h;}};
struct Action {
    std::string label;std::function<void()> fn;
    Action(std::function<void()> f):fn(std::move(f)){}
    Action(std::string s,std::function<void()> f):label(std::move(s)),fn(std::move(f)){}
};
static float font_size;
static std::vector<std::optional<Vec4>> clips;
static std::optional<Vec4> clip;
struct Text {float x,y,size;std::string text;std::optional<Vec4> clip;};
static std::vector<Text> text_draws;
static std::vector<Vec4> cards;
static int buttons,option_count;
static std::string option_detail;
static size_t Glyphs(std::string_view s){size_t count=0;for(auto c:s)if((static_cast<unsigned char>(c)&0xc0)!=0x80)++count;return count;}
void nvgFontSize(NVGcontext*,float size){font_size=size;}
float nvgTextBounds(NVGcontext*,float,float,const char* start,const char* end,float*){return float(Glyphs({start,size_t(end-start)}))*font_size*.6f;}
void nvgSave(NVGcontext*){clips.push_back(clip);}
void nvgRestore(NVGcontext*){assert(!clips.empty());clip=clips.back();clips.pop_back();}
void nvgIntersectScissor(NVGcontext*,float x,float y,float w,float h){clip=Vec4{x,y,w,h};}
namespace sphaira::ui {
namespace menu {
constexpr int MenuFlag_None=0;
struct MenuBase {
    bool focused=true,pop=false;std::map<Button,Action> actions;
    MenuBase(const char*,int){}virtual ~MenuBase()=default;
    virtual const char* GetShortTitle()const{return "";}
    virtual void Draw(NVGcontext*,Theme*){}
    virtual void Update(Controller* controller,TouchInfo*){if(controller->pressed){auto found=actions.find(*controller->pressed);if(found!=actions.end()){found->second.fn();if(controller->duplicate)found->second.fn();}}}
    void SetAction(Button b,Action a){actions.insert_or_assign(b,std::move(a));}
    bool HasFocus()const{return focused;}bool ShouldPop()const{return pop;}void SetPop(){pop=true;}
    void SetSubHeading(std::string){}
};
}
struct OptionBox {template<class F>OptionBox(std::string text,const char*,F){option_detail=std::move(text);++option_count;}};
namespace violet {void Card(NVGcontext*,Vec4 r,bool,bool=false){cards.push_back(r);}}
namespace nexusdialog {
NVGcolor Text(){return {};}
void Button(NVGcontext*,Vec4 r,const char*,bool){assert(r.x>=0&&r.y>=0&&r.x+r.w<=1280&&r.y+r.h<653);++buttons;}
}
namespace gfx {
const char* getButton(::Button b){switch(b){case ::Button::X:return "X";case ::Button::A:return "A";case ::Button::UP:return "^";default:return "v";}}
void drawText(NVGcontext*,float x,float y,float size,NVGcolor colour,const char* text){assert(colour.r==1&&colour.g==1&&colour.b==1);text_draws.push_back({x,y,size,text,clip});}
}
}
static sphaira::ui::menu::MenuBase* current;
struct App {template<class T,class... A>static void Push(A&&...args){T value(std::forward<A>(args)...);(void)value;if(current)current->focused=false;}};
using namespace sphaira::ui;
namespace ui=sphaira::ui;
namespace view=sphaira::ui::usbimportview;
// PRODUCTION_REVIEW_MENU
static void ResetDraw(){text_draws.clear();cards.clear();buttons=0;assert(clips.empty());clip.reset();}
static void CheckDraw(ReviewMenu& menu,size_t expected_rows,bool primary){
    ResetDraw();NVGcontext vg;Theme theme;menu.Draw(&vg,&theme);
    assert(cards.size()==expected_rows+1&&buttons==int(primary));assert(!clip&&clips.empty());
    assert(text_draws.size()==4+expected_rows*2);
    for(size_t i=0;i<cards.size();++i){const auto& r=cards[i];assert(r.x>=0&&r.y>=106&&r.x+r.w<=1280&&r.y+r.h<=639);if(i>1)assert(cards[i-1].y+cards[i-1].h<=r.y);}
    for(const auto& t:text_draws){assert(t.x>=0&&t.y>=106&&t.y+t.size<=639);const auto width=float(Glyphs(t.text))*t.size*.6f;
        if(t.clip){assert(t.x>=t.clip->x&&t.y>=t.clip->y);assert(t.x+width<=t.clip->x+t.clip->w+.01f);assert(t.y+t.size<=t.clip->y+t.clip->h);}
        else assert(t.x+width<=1280);
    }
}
int main(){
    std::string long_text(3000,'W');std::vector<ReviewRow> rows;
    for(int i=0;i<103;++i)rows.push_back({std::to_string(i)+long_text,long_text,"Complete detail "+std::to_string(i)+long_text});
    ReviewMenu menu(long_text,long_text,long_text,rows);current=&menu;
    int primary=0,secondary=0;
    menu.SetPrimary("Add ready to HOME",[&]{++primary;menu.focused=false;});
    menu.SetSecondary("Prepare USB",[&]{++secondary;menu.focused=false;});
    CheckDraw(menu,5,true);
    assert(text_draws[0].text.ends_with("..."));assert(text_draws[1].clip->w==800);
    Controller controller{Button::RIGHT};TouchInfo touch{};
    menu.Update(&controller,&touch);CheckDraw(menu,5,true);assert(text_draws[3].text.starts_with("5"));
    for(int i=0;i<30;++i)menu.Update(&controller,&touch);
    CheckDraw(menu,3,true);assert(text_draws[3].text.starts_with("100"));
    controller={Button::A,true};menu.Update(&controller,&touch);assert(option_count==1&&option_detail==rows[102].detail);
    // A pushed a modal; stale controller/footer/custom-touch callbacks do nothing.
    touch={true,900,150};controller={Button::X,true};menu.Update(&controller,&touch);assert(primary==0);
    menu.focused=true;menu.Update(&controller,&touch);assert(primary==1);
    menu.focused=true;controller={Button::Y,true};touch={};menu.Update(&controller,&touch);assert(secondary==1);
    menu.focused=true;controller={Button::B,true};touch={true,900,150};menu.Update(&controller,&touch);assert(menu.ShouldPop()&&primary==1);
    menu.actions.at(Button::X).fn();menu.actions.at(Button::Y).fn();menu.actions.at(Button::A).fn();assert(primary==1&&secondary==1&&option_count==1);
    ReviewMenu empty(long_text,long_text,long_text,{});current=&empty;CheckDraw(empty,0,false);assert(text_draws[0].clip->w==1140);
    controller={Button::RIGHT};touch={};empty.Update(&controller,&touch);controller={Button::A};empty.Update(&controller,&touch);assert(option_count==1);
    std::puts("PASS actual ReviewMenu Draw: bounded measured text, UTF-8 ellipsis, 5-row paging/103 entries, white text, full details, focus/pop and duplicate-input guards");
}
