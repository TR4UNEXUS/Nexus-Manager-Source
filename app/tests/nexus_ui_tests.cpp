// SPDX-License-Identifier: GPL-3.0-or-later
// Host reference using native layout and icon geometry, not a Switch screenshot.
#include "ui/nexus_layout.hpp"
#include "ui/nexus_icons.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
using namespace sphaira::ui::nexusview;
struct SvgPainter {
    std::ostringstream xml;unsigned primitives{};
    void Check(float x,float y){assert(std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=96&&y>=0&&y<=96);}
    void Path(std::initializer_list<Point> points,bool close){
        assert(points.size()>=2);++primitives;xml<<"<path d='";bool first=true;
        for(auto p:points){Check(p.x,p.y);xml<<(first?"M":"L")<<p.x<<" "<<p.y<<" ";first=false;}
        if(close)xml<<"Z";xml<<"'/>";
    }
    void Rect(float x,float y,float w,float h,float radius){Check(x,y);Check(x+w,y+h);++primitives;xml<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<w<<"' height='"<<h<<"' rx='"<<radius<<"'/>";}
    void Circle(float x,float y,float radius){Check(x-radius,y-radius);Check(x+radius,y+radius);++primitives;xml<<"<circle cx='"<<x<<"' cy='"<<y<<"' r='"<<radius<<"'/>";}
};
void Text(std::ostringstream& s,float x,float y,unsigned size,const char* text,const char* ="#ffffff"){
    // Mirror the production text-only contrast rule; accent geometry keeps its colour.
    s<<"<text x='"<<x<<"' y='"<<y<<"' font-size='"<<size<<"' dominant-baseline='hanging' fill='#ffffff'>"<<text<<"</text>";
}
void Body(std::ostringstream& s,Rect r,unsigned size,const char* text){
    s<<"<foreignObject x='"<<r.x<<"' y='"<<r.y<<"' width='"<<r.w<<"' height='"<<r.h<<"'><div xmlns='http://www.w3.org/1999/xhtml' style='font:"<<size<<"px/1.25 Arial,sans-serif;color:#ffffff;overflow:hidden'>";
    for(const char* p=text;*p;++p){if(*p=='\n')s<<"<br/>";else s<<*p;}
    s<<"</div></foreignObject>";
}
std::string Shape(Rect r,float cut){
    std::ostringstream p;p<<"M"<<r.x+cut<<" "<<r.y<<" H"<<r.x+r.w-cut<<" L"<<r.x+r.w<<" "<<r.y+cut<<" V"<<r.y+r.h-cut<<" L"<<r.x+r.w-cut<<" "<<r.y+r.h<<" H"<<r.x+cut<<" L"<<r.x<<" "<<r.y+r.h-cut<<" V"<<r.y+cut<<" Z";return p.str();
}
void DrawCard(std::ostringstream& s,Rect r,bool selected=false,bool compact=false){
    const auto shape=Shape(r,compact?8:18);
    s<<"<path d='"<<shape<<"' fill='url(#"<<(selected?"selected":"card")<<")'/>";
    if(selected){
        s<<"<path d='"<<shape<<"' fill='none' stroke='#f540e7' stroke-opacity='"<<30.0/255<<"' stroke-width='9'/>";
        s<<"<path d='"<<shape<<"' fill='none' stroke='#f540e7' stroke-opacity='"<<72.0/255<<"' stroke-width='5'/>";
    }
    s<<"<path d='"<<shape<<"' fill='none' stroke='url(#"<<(selected?"selectedEdge":"edge")<<")' stroke-width='"<<(selected?2:1)<<"'/>";
    if(!compact){
        s<<"<path d='"<<Shape({r.x+6,r.y+6,r.w-12,r.h-12},14)<<"' fill='none' stroke='#aa73de' stroke-opacity='"<<(selected?85.0:35.0)/255<<"'/>";
        s<<"<rect x='"<<r.x+24<<"' y='"<<r.y+1<<"' width='"<<(selected?90:44)<<"' height='2' fill='"<<(selected?"#f540e7":"#bea4f5")<<"'/>";
        for(unsigned i=0;i<4;++i)s<<"<rect x='"<<r.x+r.w-42+i*6<<"' y='"<<r.y+r.h-13<<"' width='2' height='3' fill='#bf90ff' fill-opacity='"<<90.0/255<<"'/>";
    }
}
void Icon(std::ostringstream& s,MenuIcon icon,float x,float y,float size,const char* colour="#f540e7"){
    SvgPainter p;PaintIcon(p,icon);assert(p.primitives>0);
    s<<"<g transform='translate("<<x<<","<<y<<") scale("<<size/96<<")' fill='none' stroke='"<<colour<<"' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round'>"<<p.xml.str()<<"</g>";
}
struct FooterAction {const char* symbol;const char* label;float label_width;};
void Footer(std::ostringstream& s,std::initializer_list<FooterAction> actions){
    // Same right-to-left action order/gaps as Widget::SetupUiButtons. Circles
    // approximate NintendoExt's Switch glyphs; host fonts are not embedded.
    float x=1220;
    for(const auto& a:actions){
        s<<"<text x='"<<x<<"' y='675' font-size='20' dominant-baseline='hanging' text-anchor='end' fill='#ffffff'>"<<a.label<<"</text>";
        const float centre=x-a.label_width-8-13;
        s<<"<circle cx='"<<centre<<"' cy='684' r='13' fill='#ffffff'/><text class='switch-symbol-cutout' x='"<<centre<<"' y='684' text-anchor='middle' dominant-baseline='central' font-size='18' font-weight='bold' fill='#130b20'>"<<a.symbol<<"</text>";
        x-=a.label_width+8+26+34;
    }
    assert(x>255); // preserve the left-side page indicator.
}
std::string Base64(const std::string& input){
    constexpr const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;unsigned value=0,bits=0;
    for(unsigned char c:input){value=(value<<8)|c;bits+=8;while(bits>=6){bits-=6;output+=table[(value>>bits)&63];}}
    if(bits)output+=table[(value<<(6-bits))&63];
    while(output.size()%4)output+='=';return output;
}
std::ostringstream Base(const std::string& background){
    std::ostringstream s;s<<"<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='720'><defs><linearGradient id='card' x1='0' y1='0' x2='1' y2='1'><stop stop-color='#1c122c' stop-opacity='.965'/><stop offset='1' stop-color='#0d0918' stop-opacity='.976'/></linearGradient><linearGradient id='selected' x1='0' y1='0' x2='1' y2='1'><stop stop-color='#39184d' stop-opacity='.973'/><stop offset='1' stop-color='#0d0918' stop-opacity='.976'/></linearGradient><linearGradient id='edge' x1='0' y1='0' x2='1' y2='1'><stop stop-color='#805caa'/><stop offset='1' stop-color='#41285b'/></linearGradient><linearGradient id='selectedEdge' x1='0' y1='0' x2='1' y2='1'><stop stop-color='#ffc7fa'/><stop offset='1' stop-color='#f540e7'/></linearGradient></defs><rect width='1280' height='720' fill='#0c0714'/>";
    if(!background.empty())s<<"<image width='1280' height='720' preserveAspectRatio='none' href='data:image/png;base64,"<<background<<"'/>";
    s<<"<rect x='42' y='100' width='1196' height='92' fill='#0e0817'/><rect width='1280' height='100' fill='#0c0714' fill-opacity='.957'/><path d='M30 98H1250' stroke='#633484'/><path d='M54 98H208' stroke='#f540e7' stroke-width='2'/><g font-family='Arial,sans-serif'>";
    Text(s,54,21,32,"NEXUS","#f540e7");Text(s,202,29,21,"MANAGER");Text(s,55,66,13,"LAYOUT PREVIEW / EXAMPLE LIBRARY","#bea4f5");
    const char* names[]={"SD card","Console","USB"};const char* sizes[]={"32.4 GB free","18.6 GB free","402.1 GB free"};
    const char* accents[]={"#c9a1ff","#b8a6ff","#f540e7"};
    for(unsigned i=0;i<3;++i){const float x=600+210*i;DrawCard(s,{x,16,198,73},false,true);Text(s,x+14,25,15,names[i],accents[i]);Text(s,x+14,47,17,sizes[i]);s<<"<rect x='"<<x+14<<"' y='74' width='170' height='4' rx='2' fill='#432a54'/><rect x='"<<x+14<<"' y='74' width='"<<(i==0?118:i==1?71:34)<<"' height='4' rx='2' fill='"<<accents[i]<<"'/>";}
    DrawCard(s,{30,653,1220,52},false,true);return s;
}
int main(int argc,char** argv){
    static_assert(HomeCount==4 && PageSize==6);
    // Keep every existing use compatible and validate all native vector paths,
    // including the four dedicated home choices, on the same 96px canvas.
    for(auto icon:{MenuIcon::Installed,MenuIcon::Sd,MenuIcon::Usb,MenuIcon::Cleanup,
            MenuIcon::Homebrew,MenuIcon::Mods,MenuIcon::Folder,MenuIcon::HomeLibrary,
            MenuIcon::HomeInstall,MenuIcon::FileManager,MenuIcon::More}){
        SvgPainter p;PaintIcon(p,icon);assert(p.primitives>0);
    }
    assert(Base64("Man")=="TWFu" && Base64("M")=="TQ==");
    std::string background;
    if(argc>=2){std::ifstream input(argc>=3?argv[2]:"assets/romfs/theme/nexus-violet-background.png",std::ios::binary);assert(input.good());const std::string bytes{std::istreambuf_iterator<char>(input),{}};assert(bytes.size()>8 && bytes.substr(1,3)=="PNG");background=Base64(bytes);}
    auto home=Base(background);DrawCard(home,{42,106,1196,92},false,true);
    Text(home,64,121,32,"Your games, where you want them");Text(home,64,165,19,"Move a game to USB, install something new, or browse your files.","#bea4f5");
    const char* names[]={"My games","Install games","File manager","More"};
    const char* descriptions[]={"Move games to USB.\nFree up space on SD.","Choose a package from\nSD or USB to install.","Copy, cut, paste and delete.\nSD card and USB.","Apps, saves and mods.\nPC transfer, help and tools."};
    const MenuIcon icons[]={MenuIcon::HomeLibrary,MenuIcon::HomeInstall,MenuIcon::FileManager,MenuIcon::More};
    for(size_t i=0;i<HomeCount;++i){
        const auto r=HomeCard(i);DrawCard(home,r,i==0);
        home<<"<rect x='"<<r.x+22<<"' y='"<<r.y+24<<"' width='92' height='92' rx='16' fill='#f540e7' fill-opacity='"<<(i==0?.16:.08)<<"'/>";
        Icon(home,icons[i],r.x+32,r.y+34,72,i==0?"#f540e7":"#bea4f5");
        Text(home,r.x+22,r.y+130,27,names[i]);Body(home,{r.x+22,r.y+180,r.w-44,94},20,descriptions[i]);
    }
    Text(home,55,584,20,"Start with My games to free space on your SD card.","#bea4f5");Text(home,55,615,16,"More keeps your existing tools together, including recovery and updates.","#bea4f5");
    Text(home,55,674,17,"Home","#bea4f5");Footer(home,{{"A","Open",48},{"+","Exit",34},{"&#8722;","Help &amp; options",139}});home<<"</g></svg>";
    auto library=Base(background);DrawCard(library,{42,106,1196,533});Text(library,54,116,28,"My games");Text(library,690,123,18,"Page 1 / 2","#bea4f5");
    const char* titles[]={"Left 4 Dead","A game with a longer title","Adventure Collection","Puzzle World","Racing Championship","My homebrew app"};
    const char* locations[]={"SD","Check storage with A","USB","Check storage with A","Check storage with A","USB app"};
    for(size_t i=0;i<PageSize;++i){const auto r=Card(i);DrawCard(library,r,i==0);Icon(library,MenuIcon::Installed,r.x+8,r.y+7,52);Text(library,r.x+76,r.y+9,22,titles[i]);Text(library,r.x+76,r.y+40,16,locations[i],i?"#bea4f5":"#f540e7");}
    const auto d=DetailPanel();DrawCard(library,d,false,true);Icon(library,MenuIcon::Installed,d.x+(d.w-128)/2,d.y+18,128);
    Text(library,d.x+20,d.y+162,24,titles[0]);Text(library,d.x+20,d.y+239,18,"SD","#f540e7");
    Body(library,{d.x+20,d.y+275,d.w-40,63},18,"Review the destination before moving. Your saves stay unchanged.");
    const auto button=PrimaryAction();DrawCard(library,button,true,true);Text(library,button.x+86,button.y+15,22,"X  Move to USB","#f540e7");
    Text(library,54,615,17,"A: more actions     Left / right: change page     B: home","#bea4f5");
    Text(library,55,674,17,"1 / 12","#bea4f5");Footer(library,{{"A","Manage",73},{"B","Back",45},{"X","Move to USB",118},{"Y","Refresh",70},{"+","Exit",34},{"&#8722;","Help",41}});library<<"</g></svg>";
    if(argc>=2){std::ofstream a(argv[1]);a<<home.str();assert(a.good());std::ofstream b(std::string(argv[1])+".library.svg");b<<library.str();assert(b.good());}
    std::cout<<"PASS Manager previews use original Nexus artwork/purple chrome, native four-card/six-row/detail/action geometry and shared icon paths\n";
    return 0;
}
