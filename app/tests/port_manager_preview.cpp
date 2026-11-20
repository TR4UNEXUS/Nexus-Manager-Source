// SPDX-License-Identifier: GPL-3.0-or-later
#define main NexusDashboardPreviewMain
#include "nexus_ui_tests.cpp"
#undef main
int main(int argc,char** argv){
 assert(argc==2);std::ifstream input("assets/romfs/theme/nexus-violet-background.png",std::ios::binary);assert(input);const std::string bytes{std::istreambuf_iterator<char>(input),{}};const auto background=Base64(bytes);
 for(int page=0;page<3;++page){auto s=Base(background);DrawCard(s,{42,106,1196,533});
  const char* titles[]={"Manage port / Grand Theft Auto V","Port mods / Left 4 Dead","Ports &amp; homebrew"};Text(s,62,117,26,titles[page]);Text(s,62,151,16,page==0?"SD card / atmosphere/contents/0100b00b51230000":"USB drive / switch/left4dead/wine-nx-runtime.nro");
  const char* names[]={"Create HOME shortcut","Manage mods","Linked folders","Move or copy port","Delete port files","Remove HOME application"};
  const char* info[]={"Launch the NRO from HOME","Import a folder or ZIP, enable or restore originals","Review or add data folders","Transfer the port between SD and USB","Review every folder before permanent deletion","Keep the port files and saved progress"};
  for(int i=0;i<(page==0?4:7);++i){const float y=180+i*58;DrawCard(s,{54,y,1172,54},i==1,true);
   s<<"<defs><clipPath id='name"<<i<<"'><rect x='74' y='"<<y+7<<"' width='955' height='28'/></clipPath><clipPath id='info"<<i<<"'><rect x='74' y='"<<y+33<<"' width='955' height='22'/></clipPath></defs><g clip-path='url(#name"<<i<<")'>";
   Text(s,74,y+7,20,page==0?names[(int[]){1,2,4,5}[i]]:page==1?(i==0?"Texture overhaul.zip":i==1?"Controller and interface improvements":"A very long mod name with extra details about the game edition and version that must stay in its column.zip"):(i==0?"Grand Theft Auto V":i==1?"Left 4 Dead":"Another installed port with a long title and edition name"));s<<"</g><g clip-path='url(#info"<<i<<")'>";
   Text(s,74,y+33,14,page==0?info[(int[]){1,2,4,5}[i]]:page==1?"/switch/left4dead / 1.2 GB":"/switch/left4dead/wine-nx-runtime.nro");s<<"</g>";Text(s,1070,y+18,16,page==0?"Open":page==1?(i==0?"Enabled":i==1?"Recover":"Disabled"):"HOME linked");
  }
  Text(s,62,605,16,page==1?"Enable a mod to apply its files. Disable restores the originals.":"Manage the port's files, mods and HOME shortcut separately.");Text(s,55,674,17,"2 / 7");
  if(page==1)Footer(s,{{"A","Manage",70},{"B","Back",45},{"X","Import mod",100},{"Y","Refresh",70},{"ZR","Delete mod",100},{"+","Exit",34}});
  else Footer(s,{{"A","Manage",70},{"B","Back",45},{"Y","Refresh",70},{"+","Exit",34}});
  s<<"</g></svg>";std::ofstream file(std::string(argv[1])+std::to_string(page)+".svg");file<<s.str();assert(file);
 }
 std::cout<<"Port manager previews generated: six actions, seven mod/library rows, white labels and Switch footer controls\n";
}
