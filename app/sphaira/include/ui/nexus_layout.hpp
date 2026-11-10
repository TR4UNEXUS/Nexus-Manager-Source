// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>

namespace sphaira::ui::nexusview {
inline constexpr std::size_t Columns=1, Rows=6, PageSize=Columns*Rows;
struct Rect { float x,y,w,h; };
inline constexpr Rect HomeCard(std::size_t index) {
    return {54.f+(index%4)*300.f,228.f,276.f,312.f};
}
inline constexpr std::size_t HomeCount=4;
inline constexpr std::size_t HomeVertical(std::size_t index,bool down) {
    return down?std::min(index+1,HomeCount-1):(index?index-1:0);
}
inline constexpr std::size_t MoreCount=6,MoreColumns=3;
inline constexpr Rect MoreCard(std::size_t index){return {54.f+(index%MoreColumns)*394.f,174.f+(index/MoreColumns)*210.f,374.f,192.f};}
inline constexpr std::size_t MoreVertical(std::size_t index,bool down){
    return down?(index+MoreColumns<MoreCount?index+MoreColumns:index):(index>=MoreColumns?index-MoreColumns:index);
}
inline constexpr std::size_t PageStart(std::size_t selected) { return selected/PageSize*PageSize; }
inline constexpr Rect Card(std::size_t index) {
    const auto slot=index%PageSize;
    return {54.f,164.f+slot*72.f,748.f,66.f};
}
inline constexpr Rect DetailPanel() { return {822.f,164.f,404.f,426.f}; }
inline constexpr Rect PrimaryAction() { return {842.f,518.f,364.f,52.f}; }
inline constexpr std::size_t Vertical(std::size_t selected,std::size_t count,bool down) {
    if(!count)return 0;
    selected=std::min(selected,count-1);
    if(!down)return selected>=Columns?selected-Columns:selected;
    const auto next_row=(selected/Columns+1)*Columns;
    return next_row<count?std::min(selected+Columns,count-1):selected;
}
}
