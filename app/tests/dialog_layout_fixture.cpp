// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "nanovg.h"
using s64=std::int64_t;using Result=unsigned;
constexpr float SCREEN_WIDTH=1280,SCREEN_HEIGHT=720;
struct Vec2{float x,y;};struct Vec4{float x,y,w,h;};struct Theme{NVGcolor GetColour(int){return nvgRGB(255,255,255);}};
constexpr int ThemeEntryID_TEXT=0,ThemeEntryID_TEXT_INFO=1,ThemeEntryID_TEXT_SELECTED=2;
struct Guard{std::function<void()> done;~Guard(){done();}};
#define COMBINE_(a,b) a##b
#define COMBINE(a,b) COMBINE_(a,b)
#define ON_SCOPE_EXIT(...) Guard COMBINE(guard_,__LINE__){[&]{__VA_ARGS__;}}
struct TouchInfo{bool is_clicked{};float x{},y{};bool in_range(Vec4 r)const{return x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h;}};
namespace sphaira {enum class Button{A,B,UP,DOWN,LEFT,RIGHT};}
namespace sphaira::ui {
struct Action{std::function<void()> callback;explicit Action(std::function<void()> fn):callback(std::move(fn)){}};
struct Controller{std::optional<Button> pressed;};
struct Widget{
    Vec4 m_pos{};std::map<Button,Action> actions;bool pop{};
    virtual ~Widget()=default;virtual void Draw(NVGcontext*,Theme*)=0;virtual void Update(Controller* c,TouchInfo*){if(c->pressed){auto cb=actions.at(*c->pressed).callback;c->pressed.reset();cb();}}
    virtual void OnFocusGained()noexcept{}virtual void OnFocusLost()noexcept{}
    void SetAction(Button button,Action action){actions.insert_or_assign(button,std::move(action));}
    template<class... T>void SetActions(T... pairs){(SetAction(pairs.first,std::move(pairs.second)),...);}
    void SetPop(){pop=true;}void SetHidden(bool){}Vec4 GetPos()const{return m_pos;}void FireAction(Button b){auto cb=actions.at(b).callback;cb();}
};
namespace capture{struct Box{Vec4 bounds;float font;std::string text;};std::vector<Box> boxes;std::vector<Vec4> clips,fills;unsigned triangles{};}
namespace gfx {
void drawRect(NVGcontext* vg,float x,float y,float w,float h,const NVGcolor& color,float=0){nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillColor(vg,color);nvgFill(vg);}
void drawRect(NVGcontext* vg,Vec4 r,const NVGcolor& c,float radius=0){drawRect(vg,r.x,r.y,r.w,r.h,c,radius);}
void drawText(NVGcontext* vg,float x,float y,float font,const NVGcolor&,const char* text,int align=NVG_ALIGN_LEFT|NVG_ALIGN_TOP){nvgTextAlign(vg,align);nvgFontSize(vg,font);nvgFillColor(vg,nvgRGB(255,255,255));nvgText(vg,x,y,text,nullptr);}
void drawTextArgs(NVGcontext* vg,float x,float y,float font,int align,const NVGcolor& c,const char* format,...){char text[256];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);drawText(vg,x,y,font,c,text,align);}
void drawTextBox(NVGcontext* vg,float x,float y,float font,float width,const NVGcolor&,const char* text,int align=NVG_ALIGN_LEFT|NVG_ALIGN_TOP){assert(width>0);nvgTextAlign(vg,align);nvgFontSize(vg,font);nvgFillColor(vg,nvgRGB(255,255,255));float b[4]{};nvgTextBoxBounds(vg,x,y,width,text,nullptr,b);capture::boxes.push_back({{x,y,width,b[3]-b[1]},font,text});nvgTextBox(vg,x,y,width,text,nullptr);}
void drawImage(NVGcontext*,Vec4,int,float){}void dimBackground(NVGcontext*){}
const char* getButton(Button b){switch(b){case Button::A:return "A";case Button::UP:return "Up";case Button::DOWN:return "Down";default:return "B";}}
}
}
namespace sphaira {enum class SoundEffect{Error};struct App{static inline NVGcontext* vg{};static NVGcontext* GetVg(){return vg;}static void PlaySoundEffect(SoundEffect){}};}
void log_write(const char*,...){}
namespace nexus::presentation {std::string Message(const char*,const std::string& message){return message;}}
// VIOLET
// DIALOG
namespace sphaira::ui {
struct ScrollingText{void Draw(NVGcontext* vg,bool,float x,float y,float width,float size,int align,const NVGcolor& color,const std::string& text){nvgSave(vg);nvgIntersectScissor(vg,x,0,width,720);gfx::drawText(vg,x,y,size,color,text.c_str(),align);nvgRestore(vg);}};
struct SidebarEntryBase {Vec4 m_pos{910,170,310,70};std::string m_title,m_info,m_depends_info;ScrollingText m_scolling_title,m_scolling_info_title,m_scolling_value;bool HasFocus(){return true;}bool IsEnabled(){return true;}void Draw(NVGcontext*,Theme*,const Vec4&,bool);void DrawEntry(NVGcontext*,Theme*,const std::string&,const std::string&,bool);};
}
// SIDEBAR_DRAW
// OPTION_HEADER
// OPTION_SOURCE
// ERROR_HEADER
// ERROR_METHODS
std::map<int,std::pair<int,int>> textures;int next_texture{};
int Create(void*){return 1;}int CreateTexture(void*,int,int w,int h,int,const unsigned char*){textures[++next_texture]={w,h};return next_texture;}
int DeleteTexture(void*,int){return 1;}int UpdateTexture(void*,int,int,int,int,int,const unsigned char*){return 1;}
int TextureSize(void*,int id,int* w,int* h){*w=textures.at(id).first;*h=textures.at(id).second;return 1;}
void Viewport(void*,float,float,float){}void Noop(void*){}
void Fill(void*,NVGpaint*,NVGcompositeOperationState,NVGscissor*,float,const float* b,const NVGpath*,int){sphaira::ui::capture::fills.push_back({b[0],b[1],b[2]-b[0],b[3]-b[1]});}
void Stroke(void*,NVGpaint*,NVGcompositeOperationState,NVGscissor*,float,float,const NVGpath*,int){}
void Triangles(void*,NVGpaint* paint,NVGcompositeOperationState,NVGscissor* clip,const NVGvertex*,int,float){
    assert(paint->innerColor.r==1&&paint->innerColor.g==1&&paint->innerColor.b==1);++sphaira::ui::capture::triangles;
    if(clip->extent[0]>=0&&clip->extent[1]>=0){Vec4 r{clip->xform[4]-clip->extent[0],clip->xform[5]-clip->extent[1],2*clip->extent[0],2*clip->extent[1]};assert(r.w>=0&&r.h>=0&&r.x>=0&&r.y>=0&&r.x+r.w<=1280.1&&r.y+r.h<=720.1);sphaira::ui::capture::clips.push_back(r);}
}
using namespace sphaira::ui;
using sphaira::Button;
void Press(Widget& box,Button button){Controller controller{button};TouchInfo touch;box.Update(&controller,&touch);}
void Frame(NVGcontext* vg,Widget& box){capture::boxes.clear();capture::clips.clear();capture::triangles=0;nvgBeginFrame(vg,1280,720,1);box.Draw(vg,nullptr);nvgEndFrame(vg);assert(capture::triangles>0);}
int main(int argc,char** argv){
    assert(argc==2);NVGparams params{};params.renderCreate=Create;params.renderCreateTexture=CreateTexture;params.renderDeleteTexture=DeleteTexture;params.renderUpdateTexture=UpdateTexture;params.renderGetTextureSize=TextureSize;params.renderViewport=Viewport;params.renderCancel=Noop;params.renderFlush=Noop;params.renderFill=Fill;params.renderStroke=Stroke;params.renderTriangles=Triangles;params.renderDelete=Noop;
    auto* vg=nvgCreateInternal(&params);assert(vg);sphaira::App::vg=vg;assert(nvgCreateFont(vg,"ui",argv[1])>=0);nvgFontFace(vg,"ui");
    std::string report="NAS install finished\n\n";for(unsigned i=0;i<70;++i)report+="Game "+std::to_string(i)+": Installed to USB. Original file was kept on the network server.\n";
    unsigned callbacks{};std::optional<s64> choice;
    OptionBox longbox(report,"Back","Continue",0,[&](auto selected){++callbacks;choice=selected;});Frame(vg,longbox);assert(capture::boxes.back().font==22);const auto first_y=capture::boxes.back().bounds.y;
    Press(longbox,Button::DOWN);Frame(vg,longbox);assert(capture::boxes.back().bounds.y==first_y-66&&!longbox.pop&&callbacks==0);
    for(unsigned i=0;i<200;++i)Press(longbox,Button::DOWN);Frame(vg,longbox);const auto last_y=capture::boxes.back().bounds.y;Press(longbox,Button::DOWN);Frame(vg,longbox);assert(capture::boxes.back().bounds.y==last_y);
    for(unsigned i=0;i<200;++i)Press(longbox,Button::UP);Frame(vg,longbox);assert(capture::boxes.back().bounds.y==first_y);
    Press(longbox,Button::RIGHT);Press(longbox,Button::A);assert(longbox.pop&&callbacks==1&&choice==1);
    OptionBox cancel("Do this?\n\nA small confirmation.","Cancel","Install",0,[&](auto selected){++callbacks;choice=selected;});Frame(vg,cancel);Press(cancel,Button::DOWN);Press(cancel,Button::B);assert(cancel.pop&&callbacks==2&&!choice);
    OptionBox hugeheading(std::string(3000,'W')+"\n\nThe full details are still accessible.","OK",[](auto){});Frame(vg,hugeheading);assert(capture::boxes.size()==1&&capture::boxes.front().font==22&&capture::boxes.front().bounds.y>100);Press(hugeheading,Button::DOWN);Frame(vg,hugeheading);assert(capture::boxes.front().bounds.y<148);
    OptionBox image(report,"Close",[](auto){},3,false);Frame(vg,image);assert(capture::boxes.back().bounds.x==352&&capture::boxes.back().bounds.w==756);Press(image,Button::DOWN);Frame(vg,image);
    ErrorBox error(report);Frame(vg,error);assert(capture::boxes.back().font==22);const auto error_y=capture::boxes.back().bounds.y;Press(error,Button::DOWN);Frame(vg,error);assert(capture::boxes.back().bounds.y==error_y-66);Press(error,Button::B);assert(error.pop);
    ErrorBox touch_error("Short error");Frame(vg,touch_error);Controller c;TouchInfo touch{true,640,580};touch_error.Update(&c,&touch);assert(touch_error.pop);
    nvgBeginFrame(vg,1280,720,1);nexusdialog::Body(vg,{0,0,0,-4},"Never draw negative layout");nvgEndFrame(vg);
    SidebarEntryBase sidebar;Theme theme;sidebar.m_title=std::string(200,'W');sidebar.m_info=report;
    capture::fills.clear();nvgBeginFrame(vg,1280,720,1);sidebar.Draw(vg,&theme,{880,0,400,720},false);nvgEndFrame(vg);for(const auto& fill:capture::fills)assert(fill.x>=0&&fill.y>=0&&fill.x+fill.w<=1280&&fill.y+fill.h<=720);
    for(float width:{310.f,40.f}){sidebar.m_pos.w=width;capture::clips.clear();nvgBeginFrame(vg,1280,720,1);sidebar.DrawEntry(vg,&theme,std::string(200,'W'),std::string(200,'W'),true);nvgEndFrame(vg);for(const auto& clip:capture::clips){assert(clip.x>=sidebar.m_pos.x&&clip.x+clip.w<=sidebar.m_pos.x+width+0.1&&clip.y>=sidebar.m_pos.y&&clip.y+clip.h<=sidebar.m_pos.y+sidebar.m_pos.h+0.1);}}
    sidebar.m_pos={30,170,1220,70};sidebar.m_info="Browse your SMB share, then choose SD card or USB drive.";
    capture::fills.clear();capture::clips.clear();nvgBeginFrame(vg,1280,720,1);sidebar.Draw(vg,&theme,{0,0,1280,720},false);nvgEndFrame(vg);
    assert(!capture::clips.empty());
    for(const auto& clip:capture::clips)assert(clip.x>=30&&clip.x+clip.w<=1250&&clip.y>=606&&clip.y+clip.h<=634);
    for(const auto& fill:capture::fills)assert(fill.y+fill.h<=240); // no help flyout over the full-screen list
    nvgDeleteInternal(vg);std::cout<<"Production dialogs + real NanoVG font metrics: long reports/headings, image layout, bounded white text, scroll limits, A/B dismissal and Left/Right confirmation unchanged passed\n";
}
