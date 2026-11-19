// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string_view>
namespace sphaira::ui::fileview {
inline constexpr std::size_t Rows=6;
inline constexpr std::size_t ActionCount=4;
enum class FileAction { Copy, Move, Delete, Paste };
struct Rect {float x,y,w,h;};
inline constexpr std::size_t PageRows(bool file_browser=true){return file_browser?Rows:7;}
inline constexpr Rect ActionButton(std::size_t index){return {54+static_cast<float>(index)*296,176,284,44};}
inline constexpr Rect Row(std::size_t index,bool file_browser=true){return {54,(file_browser?232.f:174.f)+static_cast<float>(index%PageRows(file_browser))*60,1172,56};}
inline constexpr Rect ClipboardLine(){return {64,608,1150,24};}
inline constexpr bool ClearCutAfterTransfer(bool cut,bool success,bool complete,std::size_t removed,bool removal_started=false){
    return cut && ((success && complete) || removed!=0 || removal_started);
}
inline constexpr bool IsSameOrChild(std::string_view candidate,std::string_view parent){
    if(candidate.empty() || parent.empty())return false;
    if(parent=="/")return candidate.front()=='/';
    return candidate==parent || (candidate.size()>parent.size() && candidate.substr(0,parent.size())==parent && candidate[parent.size()]=='/');
}
}
