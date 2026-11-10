// SPDX-License-Identifier: GPL-3.0-or-later
// Static GPU-drawn chrome: no timers, threads, texture uploads or I/O per frame.
#pragma once
#include "ui/nvg_util.hpp"
namespace sphaira::ui::violet {
inline NVGcolor Accent(){return nvgRGB(245,64,231);}
inline NVGcolor Lilac(){return nvgRGB(190,164,245);}
inline void Shape(NVGcontext* vg,const Vec4& r,float cut=14){
    nvgBeginPath(vg);nvgMoveTo(vg,r.x+cut,r.y);nvgLineTo(vg,r.x+r.w-cut,r.y);
    nvgLineTo(vg,r.x+r.w,r.y+cut);nvgLineTo(vg,r.x+r.w,r.y+r.h-cut);
    nvgLineTo(vg,r.x+r.w-cut,r.y+r.h);nvgLineTo(vg,r.x+cut,r.y+r.h);
    nvgLineTo(vg,r.x,r.y+r.h-cut);nvgLineTo(vg,r.x,r.y+cut);nvgClosePath(vg);
}
inline void Card(NVGcontext* vg,const Vec4& r,bool selected,bool compact=false){
    nvgSave(vg);
    const float cut=compact?8:18;
    Shape(vg,r,cut);
    nvgFillPaint(vg,nvgLinearGradient(vg,r.x,r.y,r.x+r.w,r.y+r.h,
        selected?nvgRGBA(57,24,77,248):nvgRGBA(28,18,44,246),nvgRGBA(13,9,24,249)));
    nvgFill(vg);
    if(selected){
        // Two inexpensive strokes give a neon edge without a blur pass.
        nvgStrokeColor(vg,nvgRGBA(245,64,231,30));nvgStrokeWidth(vg,9);nvgStroke(vg);
        nvgStrokeColor(vg,nvgRGBA(245,64,231,72));nvgStrokeWidth(vg,5);nvgStroke(vg);
    }
    nvgStrokePaint(vg,nvgLinearGradient(vg,r.x,r.y,r.x+r.w,r.y+r.h,
        selected?nvgRGB(255,199,250):nvgRGB(128,92,170),selected?Accent():nvgRGB(65,40,91)));
    nvgStrokeWidth(vg,selected?2:1);nvgStroke(vg);
    if(!compact){
        const Vec4 inner{r.x+6,r.y+6,r.w-12,r.h-12};Shape(vg,inner,14);
        nvgStrokeColor(vg,nvgRGBA(170,115,222,selected?85:35));nvgStrokeWidth(vg,1);nvgStroke(vg);
        gfx::drawRect(vg,r.x+24,r.y+1,selected?90:44,2,selected?Accent():Lilac());
        // Small corner vents echo the Hekate command deck.
        for(int i=0;i<4;++i)gfx::drawRect(vg,r.x+r.w-42+i*6,r.y+r.h-13,2,3,nvgRGBA(191,144,255,90));
    }
    nvgRestore(vg);
}
}
