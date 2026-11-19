// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/nexus_layout.hpp"
#include "ui/nexus_library.hpp"
#include <cassert>
#include <iostream>
using namespace sphaira::ui::nexusview;
static bool Separate(Rect a,Rect b){return a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y;}
static bool Contains(Rect a,Rect b){return b.x>=a.x && b.y>=a.y && b.x+b.w<=a.x+a.w && b.y+b.h<=a.y+a.h;}
int main(){
    static_assert(HomeCount==6 && Columns==1 && Rows==6 && PageSize==6);
    for(size_t i=0;i<HomeCount;++i){
        const auto a=HomeCard(i);assert(Contains({42,198,1196,441},a));
        assert(HomeVertical(i,false)<HomeCount && HomeVertical(i,true)<HomeCount);
        for(size_t j=i+1;j<HomeCount;++j)assert(Separate(a,HomeCard(j)));
    }
    assert(HomeVertical(0,false)==0 && HomeVertical(0,true)==3);
    assert(HomeVertical(3,true)==3 && HomeVertical(3,false)==0);
    static_assert(MoreCount==6 && MoreColumns==3);
    for(size_t i=0;i<MoreCount;++i){
        const auto card=MoreCard(i);assert(Contains({42,170,1196,420},card));
        assert(MoreVertical(i,false)==(i>=3?i-3:i));
        assert(MoreVertical(i,true)==(i<3?i+3:i));
        for(size_t j=i+1;j<MoreCount;++j)assert(Separate(card,MoreCard(j)));
    }
    const auto detail=DetailPanel(),action=PrimaryAction();
    assert(Contains({42,106,1196,533},detail) && Contains(detail,action));
    for(size_t count=0;count<129;++count){
        if(!count){assert(Vertical(50,0,true)==0);continue;}
        for(size_t i=0;i<count;++i){
            assert(Vertical(i,count,false)==(i?i-1:0));
            assert(Vertical(i,count,true)==std::min(i+1,count-1));
            const auto start=PageStart(i);assert(start<=i && i<start+PageSize);
            assert(start%PageSize==0 && Contains({42,150,1196,441},Card(i)));
            assert(Separate(Card(i),detail));
            for(size_t j=start;j<std::min(count,start+PageSize);++j)if(i!=j)assert(Separate(Card(i),Card(j)));
        }
    }
    // Only one freshly inspected origin can take the preferred-route shortcut.
    for(unsigned mask=0;mask<8;++mask){
        std::array<std::uint64_t,3> sizes{};unsigned origins=0;
        for(unsigned i=0;i<3;++i)if(mask&(1u<<i)){sizes[i]=4096*(i+1);++origins;}
        assert(!PrimaryRoute(sizes,false).available);
        const auto route=PrimaryRoute(sizes,true);assert(route.available==(origins==1));
        if(route.available){assert(sizes[static_cast<size_t>(route.source)]);assert(nexus::ValidMove(route.source,route.destination));}
    }
    assert(PrimaryRoute({4096,0,0},true).destination==nexus::Target::Usb);
    assert(PrimaryRoute({0,0,4096},true).destination==nexus::Target::Sd);
    assert(PrimaryRoute({0,4096,0},true).destination==nexus::Target::Usb);
    std::cout<<"PASS four home choices, six More cards, six-row library navigation, touch/draw geometry and inspected-origin move policy\n";
}
