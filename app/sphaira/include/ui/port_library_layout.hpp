// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
namespace nexus::portgrid {
inline constexpr std::size_t Columns=6,PageSize=12;
struct Rect {float x,y,w,h;};
inline std::size_t Start(std::size_t index){return index/PageSize*PageSize;}
inline Rect Card(std::size_t index){const auto n=index%PageSize;return {54+198.f*(n%Columns),180+210.f*(n/Columns),182,200};}
inline std::size_t Navigate(std::size_t selected,std::size_t count,int delta){
    if(!count)return 0;
    return std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(selected)+delta,0,count-1);
}
inline const char* Filter(unsigned value){return value==1?"SD card":value==2?"USB drive":"SD card + USB drive";}
}
