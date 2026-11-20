// SPDX-License-Identifier: GPL-3.0-or-later
#define main NexusDashboardPreviewMain
#include "nexus_ui_tests.cpp"
#undef main
#include "ui/port_library_layout.hpp"
int main(int argc,char** argv){
 assert(argc==2);std::ifstream input("assets/romfs/theme/nexus-violet-background.png",std::ios::binary);assert(input);
 const std::string background{std::istreambuf_iterator<char>(input),{}};
 std::ifstream icon_file("assets/romfs/theme/icon_nro.png",std::ios::binary);assert(icon_file);const std::string icon{std::istreambuf_iterator<char>(icon_file),{}};
 auto s=Base(Base64(background));DrawCard(s,{42,106,1196,533});Text(s,62,117,26,"Ports &amp; homebrew");Text(s,62,151,16,nexus::portgrid::Filter(0));
 const char* names[]={"Left 4 Dead","Left 4 Dead","Grand Theft Auto V","Half-Life 2","Jazz 2 Resurrection","Diablo","Fallout 2","OpenLara","Sonic 3 A.I.R.","Quake","A port with a longer title","Another port"};
 for(size_t i=0;i<12;++i){const auto r=nexus::portgrid::Card(i);DrawCard(s,{r.x,r.y,r.w,r.h},i==1,true);
  s<<"<image x='"<<r.x+19<<"' y='"<<r.y+8<<"' width='144' height='144' href='data:image/png;base64,"<<Base64(icon)<<"'/>";
  s<<"<rect x='"<<r.x+r.w-57<<"' y='"<<r.y+12<<"' width='47' height='25' rx='7' fill='"<<(i%2?"#6636b5":"#23529d")<<"'/>";
  Text(s,r.x+r.w-49,r.y+16,15,i%2?"USB":"SD");
  s<<"<defs><clipPath id='title"<<i<<"'><rect x='"<<r.x+10<<"' y='"<<r.y+157<<"' width='"<<r.w-20<<"' height='39'/></clipPath></defs><g clip-path='url(#title"<<i<<")'>";
  Text(s,r.x+10,r.y+157,17,names[i]);s<<"</g>";
 }
 Text(s,62,605,16,"Left 4 Dead / USB / switch/left4dead/wine-nx-runtime.nro");Text(s,55,674,17,"2 / 24");
 Footer(s,{{"A","Manage",70},{"B","Back",45},{"X","Add NRO",78},{"Y","Refresh",70},{"R","Filter",45},{"+","Exit",34}});
 s<<"</g></svg>";std::ofstream out(argv[1]);out<<s.str();assert(out);std::cout<<"Combined port icon grid preview generated from production card geometry\n";
}
