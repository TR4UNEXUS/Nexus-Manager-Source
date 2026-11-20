// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/transfer_activity.hpp"
#include "nanovg.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
using s64=std::int64_t;
struct Vec4{float x,y,w,h;};
namespace sphaira {enum class Button{UP,DOWN,Y};}
namespace sphaira::ui::capture {
struct Text{float x,y,font,width;std::string text;};
std::vector<Text> text;
}
namespace sphaira::ui::gfx {
void drawRect(NVGcontext* vg,float x,float y,float w,float h,NVGcolor c,float=0){assert(w>=0&&h>=0);nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillColor(vg,c);nvgFill(vg);}
void drawText(NVGcontext* vg,float x,float y,float font,NVGcolor c,const char* text,int align){
    assert(c.r==1&&c.g==1&&c.b==1);nvgFontSize(vg,font);nvgTextAlign(vg,align);
    const auto width=nvgTextBounds(vg,x,y,text,nullptr,nullptr);
    assert(x>=66&&x+width<=1215&&y>=120&&y+font<=639);
    capture::text.push_back({x,y,font,width,text});nvgFillColor(vg,c);nvgText(vg,x,y,text,nullptr);
}
const char* getButton(Button b){return b==Button::UP?"Up":b==Button::DOWN?"Down":"Y";}
}
// VIOLET
// TRANSFER_UI
std::map<int,std::pair<int,int>> textures;int next_texture{};
int Create(void*){return 1;}int CreateTexture(void*,int,int w,int h,int,const unsigned char*){textures[++next_texture]={w,h};return next_texture;}
int DeleteTexture(void*,int){return 1;}int UpdateTexture(void*,int,int,int,int,int,const unsigned char*){return 1;}
int TextureSize(void*,int id,int* w,int* h){*w=textures.at(id).first;*h=textures.at(id).second;return 1;}
void Viewport(void*,float,float,float){}void Noop(void*){}
void Fill(void*,NVGpaint*,NVGcompositeOperationState,NVGscissor*,float,const float*,const NVGpath*,int){}
void Stroke(void*,NVGpaint*,NVGcompositeOperationState,NVGscissor*,float,float,const NVGpath*,int){}
void Triangles(void*,NVGpaint*,NVGcompositeOperationState,NVGscissor* clip,const NVGvertex*,int,float){
    assert(clip->extent[0]>=0&&clip->extent[1]>=0);
    const float left=clip->xform[4]-clip->extent[0],right=clip->xform[4]+clip->extent[0];
    assert(left>=42&&right<=1238);
}
std::string Xml(std::string text){for(auto [from,to]:{std::pair{"&","&amp;"},{"<","&lt;"},{">","&gt;"},{"\"","&quot;"}}){std::size_t at=0;while((at=text.find(from,at))!=text.npos){text.replace(at,std::strlen(from),to);at+=std::strlen(to);}}return text;}
int main(int argc,char** argv){
    assert(argc==3);NVGparams params{};params.renderCreate=Create;params.renderCreateTexture=CreateTexture;params.renderDeleteTexture=DeleteTexture;params.renderUpdateTexture=UpdateTexture;params.renderGetTextureSize=TextureSize;params.renderViewport=Viewport;params.renderCancel=Noop;params.renderFlush=Noop;params.renderFill=Fill;params.renderStroke=Stroke;params.renderTriangles=Triangles;params.renderDelete=Noop;
    auto* vg=nvgCreateInternal(&params);assert(vg&&nvgCreateFont(vg,"ui",argv[1])>=0);nvgFontFace(vg,"ui");
    using namespace nexus::transfer::activity;
    using namespace sphaira::ui;
    Journal journal;journal.Start("FTP / ftp://192.0.2.20:5000 - Anonymous login",0);
    const auto file=journal.Open("Example adventure.nsp","USB",-1,0);journal.Bytes(file,1260ULL*1048576,-1);
    journal.BeginInstall("Example adventure.nsp","USB",10'000'000'000);
    for(const auto* stage:{"Opening package","Reading content information","Preparing USB files","Reading ticket","Reading certificate","Parsing content metadata","Preparing Program NCA"})journal.Stage(stage,11'000'000'000);
    journal.Streaming();
    journal.ContentFile("9d66e704ff9c4ef28a03fd955a6701ce.nca","Program","Writing NCA",3200ULL*1048576,3200ULL*1048576,11'000'000'000);
    journal.ContentBytes(1256ULL*1048576,3200ULL*1048576);
    auto state=journal.Read(211'000'000'000);Cursor cursor;
    transferui::InstallView view{true,"Writing NCA","Receiving from PC",7500,10000,true,false,false};
    nvgBeginFrame(vg,1280,720,1);transferui::Draw(vg,{42,106,1196,533},state,cursor,view);nvgEndFrame(vg);
    assert(capture::text.size()>=20);
    assert(std::any_of(capture::text.begin(),capture::text.end(),[](const auto& line){return line.text=="Overall 75%";}));
    std::ofstream svg(argv[2]);svg<<"<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='720'><rect width='1280' height='720' fill='#090f1d'/><rect x='42' y='106' width='1196' height='533' rx='18' fill='#141e32' stroke='#2c3a54'/><text x='56' y='50' font-family='Segoe UI' font-size='23' fill='white'>NEXUS MANAGER</text>";
    for(const auto& text:capture::text)svg<<"<text x='"<<text.x<<"' y='"<<text.y+text.font<<"' font-family='Segoe UI' font-size='"<<text.font<<"' fill='white'>"<<Xml(text.text)<<"</text>";
    svg<<"<rect x='66' y='382' width='1008' height='5' rx='2' fill='#434e73'/><rect x='66' y='382' width='756' height='5' rx='2' fill='#6e9df4'/><text x='580' y='684' font-family='Segoe UI' font-size='20' fill='white'>â’· Stop task</text></svg>";svg.close();
    // Oversized and multilingual filenames and error messages stay in their
    // own rows; log timestamps do not overlap message text or the footer.
    for(int scenario=0;scenario<4;++scenario){
        state.upload.name=std::string(1200,'W')+"\xe7\x95\x8c.nsp";
        state.content.name=state.upload.name;state.content.kind=std::string(1200,'W');state.content.phase=std::string(1000,'W');
        state.install_name=state.upload.name;state.connection=std::string(1600,'W');
        for(auto& line:state.lines)line.text=std::string(1000,'W');
        state.upload.expected=scenario%2?100:0;state.upload.active=scenario%2;state.upload.bytes=150;
        view.stopping=scenario==2;view.slow=scenario==3;view.stage=std::string(1400,'W');view.note=std::string(1000,'W');
        cursor.Scroll(state,-1);capture::text.clear();
        nvgBeginFrame(vg,1280,720,1);transferui::Draw(vg,{42,106,1196,533},state,cursor,view);nvgEndFrame(vg);
        for(const auto& line:capture::text)assert(line.font>=14&&line.width<=1148.1);
    }
    nvgDeleteInternal(vg);std::cout<<"Live transfer production drawing: real font metrics, long/UTF-8 filenames, log history, known/unknown sizes, stopping and delayed-storage views passed\n";
}
