// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the existing host-only Nexus chrome/icon painter, without running its
// dashboard test. Native file-row geometry comes from file_manager_layout.hpp.
#define main NexusDashboardPreviewMain
#include "nexus_ui_tests.cpp"
#undef main
#include "ui/file_manager_layout.hpp"
int main(int argc,char** argv){
    assert(argc>=2);
    std::ifstream input(argc>=3?argv[2]:"assets/romfs/theme/nexus-violet-background.png",std::ios::binary);assert(input.good());
    const std::string bytes{std::istreambuf_iterator<char>(input),{}};const auto background=Base64(bytes);
    const char* names[]={"Games","Homebrew","Screenshots","A very long filename with a full description and edition name that extends past the normal column width and must be clipped.zip","readme.txt","settings.ini","video.mp4"};
    const char* sizes[]={"Folder","Folder","Folder","1.2 GB","2.1 KB","980 B","76.4 MB"};
    for(bool empty:{false,true}){
        auto s=Base(background);DrawCard(s,{42,106,1196,533});
        Text(s,62,114,27,"File manager / USB");
        s<<"<defs><clipPath id='folder-path'><rect x='62' y='147' width='1150' height='23'/></clipPath><clipPath id='clipboard'><rect x='64' y='608' width='1150' height='24'/></clipPath></defs><g clip-path='url(#folder-path)'>";
        Text(s,62,147,15,empty?"USB:/New folder":"USB:/Games/Collection with a very long descriptive folder name/Extra content and game assets/An additional deeply nested destination with descriptive names to check clipping/Current folder");s<<"</g>";
        if(empty)Text(s,72,205,20,"This folder is empty. Paste adds your selected item here.");
        else for(size_t i=0;i<sphaira::ui::fileview::Rows;++i){
            const auto r=sphaira::ui::fileview::Row(i);DrawCard(s,{r.x,r.y,r.w,r.h},i==0,true);
            if(i<3)Icon(s,MenuIcon::Folder,72,r.y+10,32);
            s<<"<defs><clipPath id='row"<<i<<"'><rect x='114' y='"<<r.y+4<<"' width='820' height='50'/></clipPath></defs><g clip-path='url(#row"<<i<<")'>";
            Text(s,114,r.y+17,20,names[i]);s<<"</g>";
            s<<"<text x='1208' y='"<<r.y+19<<"' text-anchor='end' dominant-baseline='hanging' font-size='16' fill='#ffffff'>"<<sizes[i]<<"</text>";
        }
        const auto line=sphaira::ui::fileview::ClipboardLine();s<<"<g clip-path='url(#clipboard)'>";Text(s,line.x,line.y+3,17,"Ready to move: SD:/switch/Example game");s<<"</g>";
        Text(s,55,674,17,empty?"0 / 0":"1 / 7");
        Footer(s,{{"A","Open",48},{"B","Up",26},{"X","Actions",66},{"Y","Paste",52},{"+","Exit",34},{"&#8722;","Storage",72}});
        s<<"</g></svg>";
        std::ofstream output(std::string(argv[1])+(empty?".library.svg":""));output<<s.str();assert(output.good());
    }
    auto confirmation=Base(background);DrawCard(confirmation,{42,106,1196,533});Text(confirmation,62,114,27,"File manager / USB");
    confirmation<<"<rect width='1280' height='720' fill='black' fill-opacity='.5'/>";
    // OptionBox: 1000x568 centered, 478px content, two 500x90 button cells.
    DrawCard(confirmation,{140,76,1000,568});
    Text(confirmation,172,102,14,"NEXUS MANAGER   /   CONFIRMATION");
    confirmation<<"<text x='640' y='146' font-size='26' dominant-baseline='hanging' text-anchor='middle' fill='#ffffff'>Delete permanently?</text>";
    Body(confirmation,{170,196,940,334},22,"USB:/Games\n\n6 files, 2 folders\n1288490188 bytes\n\nOnly these reviewed items will be removed. There is no recycle bin. Cancellation or a disconnect can leave a partially deleted folder.\n\nThis permanently deletes the selected item and its contents. It cannot be undone.");
    DrawCard(confirmation,{156,566,468,60},true,true);Text(confirmation,354,585,22,"Cancel");
    DrawCard(confirmation,{656,566,468,60},false,true);Text(confirmation,860,585,22,"Delete");
    confirmation<<"</g></svg>";
    std::ofstream confirm_output(std::string(argv[1])+".delete.svg");confirm_output<<confirmation.str();assert(confirm_output.good());
    auto more=Base(background);DrawCard(more,{42,106,1196,533});Text(more,54,114,28,"More");Text(more,54,148,17,"Tools, apps and help. Choose what you want to do.");
    const char* more_names[]={"PC transfer","Cartridges","Homebrew apps","Save backups","Game mods","Help &amp; recovery"};
    const char* descriptions[]={"Send files between your\nPC and Switch.","View your cartridge or\nsave a copy.","Browse apps and create\nHOME shortcuts.","Back up game saves or\nrestore a saved backup.","Review and manage\noptional game changes.","Guides, recovery tools\nand Manager updates."};
    const MenuIcon symbols[]={MenuIcon::MorePc,MenuIcon::MoreCartridge,MenuIcon::Homebrew,MenuIcon::MoreSave,MenuIcon::Mods,MenuIcon::MoreHelp};
    for(size_t i=0;i<MoreCount;++i){const auto r=MoreCard(i);DrawCard(more,r,i==0);
        more<<"<rect x='"<<r.x+18<<"' y='"<<r.y+12<<"' width='64' height='64' rx='12' fill='#f540e7' fill-opacity='"<<(i?".08":".16")<<"'/>";
        Icon(more,symbols[i],r.x+24,r.y+18,52);Text(more,r.x+22,r.y+84,24,more_names[i]);Body(more,{r.x+22,r.y+120,r.w-44,54},18,descriptions[i]);}
    Text(more,54,610,17,"Open a tool with A. Press B to return home.");Text(more,55,674,17,"More");Footer(more,{{"A","Open",48},{"B","Back",45},{"+","Exit",34}});more<<"</g></svg>";
    std::ofstream more_output(std::string(argv[1])+".more.svg");more_output<<more.str();assert(more_output.good());
    std::cout<<"PASS file-manager preview uses shared native rows/clipboard and Nexus chrome; populated/empty paste destinations\n";
}
