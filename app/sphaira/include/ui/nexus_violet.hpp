// SPDX-License-Identifier: GPL-3.0-or-later
// Shared blue-violet surfaces, matching the Nexus HATS installer.
#pragma once
#include "ui/nvg_util.hpp"
namespace sphaira::ui::violet {
inline NVGcolor Accent(){return nvgRGB(110,157,244);}
inline NVGcolor Lilac(){return nvgRGB(154,131,227);}
inline NVGcolor Surface(){return nvgRGB(20,30,50);}
inline NVGcolor Selected(){return nvgRGB(29,43,70);}
inline NVGcolor Line(){return nvgRGB(44,58,84);}
inline void Shape(NVGcontext* vg,const Vec4& r,float radius=16){
    nvgBeginPath(vg);nvgRoundedRect(vg,r.x,r.y,r.w,r.h,radius);
}
inline void Card(NVGcontext* vg,const Vec4& r,bool selected,bool compact=false){
    nvgSave(vg);
    Shape(vg,r,compact?10:18);
    nvgFillColor(vg,selected?Selected():Surface());nvgFill(vg);
    nvgStrokeColor(vg,selected?Accent():Line());nvgStrokeWidth(vg,selected?2:1);nvgStroke(vg);
    nvgRestore(vg);
}
}
