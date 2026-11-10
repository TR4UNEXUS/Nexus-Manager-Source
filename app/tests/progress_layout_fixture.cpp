// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/progress_layout.hpp"
#include "nexus/network_rate.hpp"
#include "nexus/progress_safety.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using u8=unsigned char;using u32=unsigned;using u64=std::uint64_t;using s64=std::int64_t;
using Vec4=nexus::progressui::Rect;using ProgressRect=Vec4;
struct NVGcolor{const char* value;};
enum {NVG_ALIGN_LEFT=1,NVG_ALIGN_CENTER=2,NVG_ALIGN_TOP=8,NVG_ALIGN_MIDDLE=16};
enum ThemeEntryID{ThemeEntryID_TEXT,ThemeEntryID_PROGRESSBAR_BACKGROUND,ThemeEntryID_LINE_SEPARATOR};
struct Theme{NVGcolor GetColour(ThemeEntryID id){return {id==ThemeEntryID_TEXT?"#ffffff":"#49305e"};}};
struct DrawItem{std::string kind,text,colour;Vec4 rect,clip;float font{};};
struct State{float font=18,line_height=1.2f;int align=NVG_ALIGN_LEFT|NVG_ALIGN_TOP;Vec4 clip{0,0,1280,720};};
struct NVGcontext{State state;std::vector<State> saved;std::vector<DrawItem> draws;};
struct NVGtextRow{const char* start{};const char* end{};const char* next{};float width{};};
std::string operator""_i18n(const char* text,std::size_t size){return std::string(text,size);}
float Width(const char* start,const char* end,float font){float out{};for(auto p=start;p<end;++p)if((static_cast<unsigned char>(*p)&0xc0)!=0x80)out+=font*0.53f;return out;}
std::vector<NVGtextRow> Lines(const char* start,const char* end,float width,float font){
    std::vector<NVGtextRow> rows;if(!end)end=start+strlen(start);auto p=start;
    while(p<end){auto first=p,last=p,space=p;float w{};
        while(p<end&&*p!='\n'){
            auto next=p+1;while(next<end&&(static_cast<unsigned char>(*next)&0xc0)==0x80)++next;
            const auto add=Width(p,next,font);if(w+add>width&&p>first)break;
            if(*p==' ')space=p;w+=add;p=next;last=p;
        }
        if(p<end&&*p!='\n'&&space>first){last=space;p=space+1;}
        else if(p<end&&*p=='\n')++p;
        rows.push_back({first,last,p,Width(first,last,font)});
    }return rows;
}
void nvgSave(NVGcontext* v){v->saved.push_back(v->state);}void nvgRestore(NVGcontext* v){assert(!v->saved.empty());v->state=v->saved.back();v->saved.pop_back();}
void nvgIntersectScissor(NVGcontext* v,float x,float y,float w,float h){auto& c=v->state.clip;const auto right=std::min(c.x+c.w,x+w),bottom=std::min(c.y+c.h,y+h);c.x=std::max(c.x,x);c.y=std::max(c.y,y);c.w=std::max(0.f,right-c.x);c.h=std::max(0.f,bottom-c.y);}
void nvgFontSize(NVGcontext* v,float f){v->state.font=f;}void nvgTextAlign(NVGcontext* v,int a){v->state.align=a;}void nvgTextLineHeight(NVGcontext* v,float h){v->state.line_height=h;}
void nvgTextBounds(NVGcontext* v,float x,float y,const char* text,const char* end,float* b){b[0]=x;b[1]=y;b[2]=x+Width(text,end?end:text+strlen(text),v->state.font);b[3]=y+v->state.font*1.2f;}
void nvgTextBoxBounds(NVGcontext* v,float x,float y,float width,const char* text,const char* end,float* b){const auto rows=Lines(text,end,width,v->state.font);b[0]=x;b[1]=y;b[2]=x+width;b[3]=y+rows.size()*v->state.font*v->state.line_height;}
int nvgTextBreakLines(NVGcontext* v,const char* text,const char* end,float width,NVGtextRow* out,int maximum){const auto rows=Lines(text,end,width,v->state.font);const int count=std::min<int>(rows.size(),maximum);std::copy_n(rows.begin(),count,out);return count;}
int nvgCreateImageMem(NVGcontext*,int,const unsigned char*,std::size_t){return 1;}
namespace gfx{
void dimBackground(NVGcontext*){}
void drawText(NVGcontext* v,float x,float y,float font,NVGcolor colour,const char* text,int align){
    const auto width=Width(text,text+strlen(text),font);if(align&NVG_ALIGN_CENTER)x-=width/2;
    v->draws.push_back({"text",text,colour.value,{x,y,width,font*1.2f},v->state.clip,font});
}
void drawTextBox(NVGcontext* v,float x,float y,float font,float width,NVGcolor colour,const char* text,int align){
    for(const auto& row:Lines(text,nullptr,width,font)){
        const auto at=x+((align&NVG_ALIGN_CENTER)?(width-row.width)/2:0);
        v->draws.push_back({"body",std::string(row.start,row.end),colour.value,{at,y,row.width,font*1.2f},v->state.clip,font});y+=font*v->state.line_height;
    }
}
void drawRect(NVGcontext* v,Vec4 r,NVGcolor c,float=0){v->draws.push_back({"rect",{},c.value,r,v->state.clip});}
void drawRect(NVGcontext* v,float x,float y,float w,float h,NVGcolor c,float=0){drawRect(v,{x,y,w,h},c);}
void drawImage(NVGcontext* v,float x,float y,float w,float h,int,float){v->draws.push_back({"cover",{},"#45305f",{x,y,w,h},v->state.clip});}
void drawSpinner(NVGcontext* v,Theme*,float x,float y,float radius,double){v->draws.push_back({"spinner",{},"#f239df",{x-radius,y-radius,radius*2,radius*2},v->state.clip});}
}
namespace nexusdialog{
NVGcolor Accent(){return {"#f239df"};}
void Panel(NVGcontext* v,Vec4 r){gfx::drawRect(v,r,{"#160c25"});}
void Brand(NVGcontext* v,float x,float y,const char*){gfx::drawText(v,x,y,14,{"#ffffff"},"NEXUS MANAGER    / TRANSFER",NVG_ALIGN_LEFT|NVG_ALIGN_TOP);}
}
struct ScrollingText{void Reset(const std::string&){}void Draw(NVGcontext* v,bool,float x,float y,float width,float font,int align,NVGcolor c,const std::string& text){nvgSave(v);nvgIntersectScissor(v,x,0,width,720);gfx::drawText(v,x-10,y,font,c,text.c_str(),align);nvgRestore(v);}};
u64 tick=1'000'000'000;u64 armGetSystemTick(){return tick;}u64 armTicksToNs(u64 n){return n;}u64 armNsToTicks(u64 n){return n;}bool nexusWatchdogCanExit(){return false;}
void mutexLock(int*){}void mutexUnlock(int*){}
namespace utils{std::string formatSizeNetwork(s64){return "50.33 MB";}}
struct Button{Vec4 rect;void Draw(NVGcontext* v,Theme*){v->draws.push_back({"footer","B   Stop task","#ffffff",rect,v->state.clip,20});}};
struct Clock{unsigned GetSeconds(){return 0;}void Update(){}};
struct ProgressBox{
    Vec4 m_pos{180,127.5,920,465};int m_mutex{};std::vector<u8> m_image_data;Clock m_timestamp;
    s64 m_size=35438546392,m_offset=1063156392,m_last_offset=1063156392,m_speed=50330000;bool m_overall=true,m_finishing=false;
    std::string m_action="Moving: SD -> USB",m_title="The Legend of Zelda: Breath of the Wild",m_transfer="Copying to USB - file 6 of 12",m_progress_note,m_byte_detail;
    nexus::network::RateMeter m_network_rate;int m_image{},m_image_pending{};bool m_is_image_pending{},m_own_image{},stopping{};
    ScrollingText m_scroll_action,m_scroll_title,m_scroll_transfer;nexus::safety::ProgressState m_safety;
    nexus::safety::Operation m_operation=nexus::safety::Operation::MayWrite;
    float GetX(){return m_pos.x;}float GetY(){return m_pos.y;}float GetW(){return m_pos.w;}float GetH(){return m_pos.h;}
    bool ShouldExit(){return stopping;}void FreeImage(){m_image=0;}
    std::vector<Button> GetFooterButtons(){return {{{m_pos.x+360,m_pos.y+m_pos.h-40,200,24}}};}
    void Draw(NVGcontext*,Theme*);
};
// HELPERS_PRODUCTION
// DRAW_PRODUCTION
bool Near(float a,float b){return std::abs(a-b)<0.02f;}
std::string Escape(std::string s){for(auto pair:{std::pair{"&","&amp;"},{"<","&lt;"},{">","&gt;"},{"\"","&quot;"}}){std::size_t p=0;while((p=s.find(pair.first,p))!=s.npos){s.replace(p,strlen(pair.first),pair.second);p+=strlen(pair.second);}}return s;}
void Svg(const std::string& path,const NVGcontext& v){
    std::ofstream out(path);out<<"<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='720' viewBox='0 0 1280 720'><rect width='1280' height='720' fill='#080510'/>";
    unsigned id=0;for(const auto& item:v.draws){const auto& r=item.rect;const auto& c=item.clip;
        out<<"<clipPath id='c"<<id<<"'><rect x='"<<c.x<<"' y='"<<c.y<<"' width='"<<c.w<<"' height='"<<c.h<<"'/></clipPath><g clip-path='url(#c"<<id<<")'>";
        ++id;
        if(item.kind=="text"||item.kind=="body"||item.kind=="footer")out<<"<text x='"<<r.x<<"' y='"<<r.y+item.font<<"' font-family='DejaVu Sans,sans-serif' font-size='"<<item.font<<"' fill='"<<item.colour<<"'>"<<Escape(item.text)<<"</text>";
        else if(item.kind=="spinner")out<<"<circle cx='"<<r.x+r.w/2<<"' cy='"<<r.y+r.h/2<<"' r='"<<r.w/2<<"' stroke='"<<item.colour<<"' fill='none'/>";
        else out<<"<rect x='"<<r.x<<"' y='"<<r.y<<"' width='"<<r.w<<"' height='"<<r.h<<"' rx='8' fill='"<<item.colour<<"' stroke='#604276'/>";
        out<<"</g>";
    }out<<"</svg>";
}
int main(int argc,char** argv){
    assert(argc==2);Theme theme;unsigned cases{};
    for(bool cover:{false,true})for(bool network:{false,true})for(int mode=0;mode<5;++mode)for(bool long_text:{false,true}){
        ProgressBox box;box.m_image=cover?1:0;box.m_safety.Touch(tick);
        if(network){box.m_network_rate.Begin(0);box.m_network_rate.Add(15*1024*1024);box.m_progress_note="Reading from NAS. Keep the network and storage connected.";}
        if(mode==1)box.stopping=true;if(mode==2)box.m_safety.Touch(0),tick=20'000'000'000;
        if(mode==3)box.m_byte_detail="Copied 1024.00 GiB / 2048.00 GiB\nVerifying the installed game files";
        if(mode==4)box.m_size=box.m_offset=0,box.m_overall=false;
        if(long_text){box.m_title=std::string(300,'M')+" 日本語のゲーム";box.m_transfer=std::string(240,'W')+"\nSecond status line";box.m_progress_note=std::string(180,'W')+"\nKeep storage connected.\nMore status information.";}
        const auto layout=nexus::progressui::Make(box.m_pos,cover,network);const Vec4 above_footer{box.GetX()+32,box.GetY()+52,box.GetW()-64,layout.footer_y-box.GetY()-52};
        for(const auto& row:{layout.action,layout.title,layout.stage,layout.status,layout.advice,layout.bar,layout.percent,layout.metric})assert(nexus::progressui::Within(row,above_footer));
        assert(layout.action.y+layout.action.h<layout.title.y&&layout.title.y+layout.title.h<layout.stage.y&&layout.stage.y+layout.stage.h<layout.status.y);
        assert(layout.status.y+layout.status.h<layout.advice.y&&layout.advice.y+layout.advice.h<layout.bar.y-13);
        assert(layout.bar.y+layout.bar.h<layout.metric.y&&layout.metric.y+layout.metric.h<layout.footer_y);
        if(network)assert(nexus::progressui::Within(layout.note,above_footer)&&layout.metric.y+layout.metric.h<layout.note.y&&layout.note.y+layout.note.h<layout.footer_y);
        if(cover)assert(layout.cover.x+layout.cover.w<layout.column.x);
        NVGcontext v;box.Draw(&v,&theme);assert(v.saved.empty());
        bool rate{},units{};for(const auto& item:v.draws){
            if(item.kind=="text"||item.kind=="body"){
                assert(item.colour=="#ffffff");
                if(item.clip.y>=layout.action.y){
                    assert(item.rect.y>=item.clip.y-0.02f&&item.rect.y+item.rect.h<=item.clip.y+item.clip.h+0.02f);
                    assert(item.clip.y+item.clip.h<layout.footer_y+0.02f);
                    if(item.kind=="body")assert(nexus::progressui::Within(item.rect,item.clip));
                }
                if(item.text.find("NAS download:")!=std::string::npos)rate=true;
                if(item.text.find("MB/s")!=std::string::npos)units=true;
            }
        }
        assert(!network||rate);if(!network&&!long_text&&mode==0)assert(units);
        if(!long_text&&mode==0){
            for(const auto& item:v.draws)if(item.text==box.m_action||item.text==box.m_title||item.text==box.m_transfer){
                if(cover)assert(Near(item.rect.x,layout.column.x));else assert(Near(item.rect.x+item.rect.w/2,layout.column.x+layout.column.w/2));
            }
        }
        if(!long_text&&mode==0&&!cover&&!network)Svg(std::string(argv[1])+"/centered-transfer.svg",v);
        if(cover&&network&&long_text&&mode==0)Svg(std::string(argv[1])+"/cover-network-long-text.svg",v);
        if(!cover&&network&&mode==1&&!long_text)Svg(std::string(argv[1])+"/stopping-network.svg",v);
        if(cover&&network&&mode==2&&!long_text)Svg(std::string(argv[1])+"/slow-cover-network.svg",v);
        if(cover&&!network&&mode==0&&!long_text)Svg(std::string(argv[1])+"/cover-transfer.svg",v);
        ++cases;tick=1'000'000'000;
    }
    std::cout<<"Production progress layout: "<<cases<<" cover/network/long-text/warning/detail/empty-progress cases, centered columns, clipping, wrapping, footer clearance and speed units passed\n";
}
