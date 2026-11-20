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
void DrawCard(std::ostringstream& s,Rect r,bool selected=false,bool compact=false){
    s<<"<rect x='"<<r.x<<"' y='"<<r.y<<"' width='"<<r.w<<"' height='"<<r.h<<"' rx='"<<(compact?10:18)
     <<"' fill='"<<(selected?"#1d2b46":"#141e32")<<"' stroke='"<<(selected?"#6e9df4":"#2c3a54")<<"' stroke-width='"<<(selected?2:1)<<"'/>";
}
void Icon(std::ostringstream& s,MenuIcon icon,float x,float y,float size,const char* colour="#6e9df4"){
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
        s<<"<circle cx='"<<centre<<"' cy='684' r='13' fill='#ffffff'/><text class='switch-symbol-cutout' x='"<<centre<<"' y='684' text-anchor='middle' dominant-baseline='central' font-size='18' font-weight='bold' fill='#09101f'>"<<a.symbol<<"</text>";
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
    std::ostringstream s;s<<"<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='720'><defs><linearGradient id='page' x2='1' y2='1'><stop stop-color='#060c1a'/><stop offset='1' stop-color='#101124'/></linearGradient></defs><rect width='1280' height='720' fill='url(#page)'/>";
    s<<"<rect width='1280' height='100' fill='#09101f' fill-opacity='.957'/><path d='M54 98H1226' stroke='#2c3a54'/><g font-family='Arial,sans-serif'>";
    s<<"<image x='54' y='24' width='48' height='48' href='data:image/jpeg;base64,"<<background<<"'/>";
    Text(s,118,26,26,"NEXUS MANAGER");Text(s,119,65,15,"v0.2.22");
    const char* names[]={"SD card","Console","USB"};const char* sizes[]={"32.4 GB free","18.6 GB free","402.1 GB free"};
    const char* accents[]={"#6e9df4","#9a83e3","#6e9df4"};
    for(unsigned i=0;i<3;++i){const float x=600+210*i;Text(s,x+14,25,15,names[i]);Text(s,x+14,47,17,sizes[i]);s<<"<rect x='"<<x+14<<"' y='74' width='170' height='4' rx='2' fill='#2c3a54'/><rect x='"<<x+14<<"' y='74' width='"<<(i==0?118:i==1?71:34)<<"' height='4' rx='2' fill='"<<accents[i]<<"'/>";}
    s<<"<rect y='653' width='1280' height='67' fill='#09101f' fill-opacity='.957'/><path d='M54 653H1226' stroke='#2c3a54'/>";
    return s;
}
int main(int argc,char** argv){
    static_assert(HomeCount==6 && PageSize==6);
    for(auto icon:{MenuIcon::Installed,MenuIcon::Sd,MenuIcon::Usb,MenuIcon::Cleanup,MenuIcon::Homebrew,MenuIcon::Mods,MenuIcon::Folder,MenuIcon::HomeLibrary,MenuIcon::HomeInstall,MenuIcon::FileManager,MenuIcon::More}){SvgPainter p;PaintIcon(p,icon);assert(p.primitives>0);}
    assert(Base64("Man")=="TWFu" && Base64("M")=="TQ==");
    assert(argc>=2);
    std::ifstream input(argc>=3?argv[2]:"assets/romfs/theme/nexus-logo.jpg",std::ios::binary);assert(input.good());
    const std::string bytes{std::istreambuf_iterator<char>(input),{}};assert(bytes.size()>8 && (unsigned char)bytes[0]==0xff && (unsigned char)bytes[1]==0xd8);const auto background=Base64(bytes);
    auto home=Base(background);
    Text(home,54,128,28,"Manage your games");Text(home,54,170,18,"Install, move and organise in one place.");
    const char* names[]={"Installed games","Install games","File manager","Cleanup","SMB / NFS","More"};
    const char* descriptions[]={"Move, copy or delete games.\nKeep your saved progress.","Install from SD, USB\nor your network.","Browse and organise files\nacross your drives.","Review unused game files\nand unfinished installs.","Find your NAS or PC.\nConnect once, browse anytime.","Saves, apps, game mods\nand helpful tools."};
    const MenuIcon icons[]={MenuIcon::HomeLibrary,MenuIcon::HomeInstall,MenuIcon::FileManager,MenuIcon::Cleanup,MenuIcon::MorePc,MenuIcon::More};
    for(size_t i=0;i<HomeCount;++i){const auto r=HomeCard(i);DrawCard(home,r,i==0);Icon(home,icons[i],r.x+22,r.y+20,42,i%2?"#9a83e3":"#6e9df4");Text(home,r.x+80,r.y+28,24,names[i]);Body(home,{r.x+22,r.y+86,r.w-44,64},18,descriptions[i]);}

    Text(home,50,673,15,"Home");Footer(home,{{"A","Open",48},{"+","Exit",34},{"&#8722;","Options",69}});home<<"</g></svg>";
    auto library=Base(background);DrawCard(library,{42,106,1196,533});Text(library,54,116,28,"Installed games");
    DrawCard(library,{850,110,376,46},false,true);Text(library,915,124,20,"R   Repair USB games");Text(library,690,123,18,"Page 1 / 2");
    const char* titles[]={"Left 4 Dead","A game with a longer title","Adventure Collection","Puzzle World","Racing Championship","My homebrew app"};
    const char* locations[]={"USB","SD","SD + USB","Internal","USB","SD"};
    for(size_t i=0;i<PageSize;++i){const auto r=Card(i);DrawCard(library,r,i==0);Icon(library,MenuIcon::Installed,r.x+8,r.y+7,52);Text(library,r.x+76,r.y+9,22,titles[i]);Text(library,r.x+76,r.y+40,16,locations[i]);}
    const auto d=DetailPanel();DrawCard(library,d,false,true);Icon(library,MenuIcon::Installed,d.x+(d.w-128)/2,d.y+12,128);
    Body(library,{d.x+20,d.y+152,d.w-40,56},22,titles[0]);
    Text(library,d.x+20,d.y+216,17,"Title ID: 0100AB0000001000");
    Text(library,d.x+20,d.y+244,17,"Version: 1.2.0");
    Text(library,d.x+20,d.y+270,17,"Update: v65536  |  DLC: 2");
    Text(library,d.x+20,d.y+298,17,"Installed size: 4.26 GiB");
    Text(library,d.x+20,d.y+330,17,"SD card: Not installed");
    Text(library,d.x+20,d.y+356,17,"Internal: Not installed");
    Text(library,d.x+20,d.y+382,17,"USB drive: 4.26 GiB");
    Text(library,54,610,16,"&#8592; &#8594;  Change page");Text(library,50,673,15,"1 / 12");Footer(library,{{"A","Manage",73},{"B","Back",45},{"Y","Refresh",70}});library<<"</g></svg>";
    auto more=Base(background);DrawCard(more,{42,106,1196,533});Text(more,64,122,28,"More");
    const char* mn[]={"PC transfer","Cartridges","Homebrew apps","Save backups","Game mods","Help &amp; recovery"};
    const char* md[]={"Send files between your\nPC and Switch.","View a cartridge or\nsave a copy.","Open apps and create\nHOME shortcuts.","Back up or restore\nyour game saves.","Manage mods for\nyour games.","Repair tools, guides\nand app updates."};
    const MenuIcon mi[]={MenuIcon::MorePc,MenuIcon::MoreCartridge,MenuIcon::Homebrew,MenuIcon::MoreSave,MenuIcon::Mods,MenuIcon::MoreHelp};
    for(size_t i=0;i<MoreCount;++i){const auto r=MoreCard(i);DrawCard(more,r,i==0);Icon(more,mi[i],r.x+24,r.y+24,44,i%2?"#9a83e3":"#6e9df4");Text(more,r.x+84,r.y+35,23,mn[i]);Body(more,{r.x+24,r.y+94,r.w-48,64},19,md[i]);}
    Text(more,50,673,15,"More");Footer(more,{{"A","Open",48},{"B","Back",45},{"+","Exit",34}});more<<"</g></svg>";
    for(const auto& [suffix,svg]:{std::pair{"",home.str()},std::pair{".library.svg",library.str()},std::pair{".more.svg",more.str()}}){std::ofstream file(std::string(argv[1])+suffix);file<<svg;assert(file.good());}
    std::cout<<"PASS host previews: shared native card geometry/icons, blue-violet palette, compact Home and More, simplified library/footer\n";
}
