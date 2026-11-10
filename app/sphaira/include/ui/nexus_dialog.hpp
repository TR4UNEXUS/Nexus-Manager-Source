// SPDX-License-Identifier: GPL-3.0-or-later
// Shared Manager chrome for every modal, confirmation and action sheet.
#pragma once
#include "ui/nvg_util.hpp"
#include "ui/nexus_violet.hpp"
#include <algorithm>

namespace sphaira::ui::nexusdialog {
inline NVGcolor Accent(){return violet::Accent();}
inline NVGcolor Text(){return nvgRGB(248,242,255);}
inline NVGcolor Muted(){return nvgRGB(194,175,216);}
inline void Panel(NVGcontext* vg,const Vec4& r) {
    nvgSave(vg);
    violet::Card(vg,r,false);
    gfx::drawRect(vg,r.x+28,r.y+1,78,3,Accent(),1);
    nvgRestore(vg);
}
inline void Brand(NVGcontext* vg,float x,float y,const char* section) {
    gfx::drawText(vg,x,y,14,Accent(),"NEXUS MANAGER");
    gfx::drawText(vg,x+154,y,14,Muted(),section);
}
inline void Button(NVGcontext* vg,const Vec4& r,const char* label,bool focus) {
    if(r.w<=20 || r.h<=0)return;
    nvgSave(vg);
    violet::Card(vg,r,focus,true);
    nvgIntersectScissor(vg,r.x+10,r.y,r.w-20,r.h);
    float font=22,bounds[4]{};
    nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
    do {nvgFontSize(vg,font);nvgTextBounds(vg,0,0,label,nullptr,bounds);if(bounds[2]-bounds[0]<=r.w-24 || font<=14)break;--font;}while(true);
    gfx::drawText(vg,r.x+r.w/2,r.y+r.h/2,font,Text(),label,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
}
inline void Body(NVGcontext* vg,const Vec4& r,const char* text,float desired=22) {
    if(r.w<=0 || r.h<=0 || !text || !*text)return;
    nvgSave(vg);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgTextLineHeight(vg,1.25f);
    float font=desired,bounds[4]{};
    do {
        nvgFontSize(vg,font);nvgTextBoxBounds(vg,r.x,r.y,r.w,text,nullptr,bounds);
        if(bounds[3]-bounds[1]<=r.h || font<=14)break;
        --font;
    }while(true);
    nvgIntersectScissor(vg,r.x,r.y,r.w,r.h);
    gfx::drawTextBox(vg,r.x,r.y,font,r.w,Text(),text);
    nvgRestore(vg);
}
inline float BodyHeight(NVGcontext* vg,float width,const char* text,float font=22){
    if(width<=0 || !text || !*text)return 0;
    nvgSave(vg);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgTextLineHeight(vg,1.25f);nvgFontSize(vg,font);
    float bounds[4]{};nvgTextBoxBounds(vg,0,0,width,text,nullptr,bounds);nvgRestore(vg);return std::max(0.f,bounds[3]-bounds[1]);
}
// Reports keep a readable font instead of shrinking an arbitrary amount of
// text into a fixed dialog. Controls stay outside the clipped scroll viewport.
struct ScrollableBody {
    float offset{},maximum{};
    void ScrollBy(float amount){offset=std::clamp(offset+amount,0.f,maximum);}
    void Draw(NVGcontext* vg,const Vec4& r,const char* text,float font=22){
        if(r.w<=0 || r.h<=0 || !text || !*text){offset=maximum=0;return;}
        const float measured=BodyHeight(vg,r.w,text,font);
        const bool overflowing=measured>r.h;
        const float height=std::max(0.f,r.h-(overflowing?30.f:0.f));
        maximum=std::max(0.f,measured-height);offset=std::clamp(offset,0.f,maximum);
        if(height>0){
            nvgSave(vg);nvgIntersectScissor(vg,r.x,r.y,r.w,height);nvgTextLineHeight(vg,1.25f);
            gfx::drawTextBox(vg,r.x,r.y-offset,font,r.w,Text(),text);nvgRestore(vg);
        }
        if(overflowing){
            const auto hint=std::string(gfx::getButton(::sphaira::Button::UP))+" "+gfx::getButton(::sphaira::Button::DOWN)+"  Scroll text";
            nvgSave(vg);nvgIntersectScissor(vg,r.x,r.y+height,r.w,r.h-height);
            gfx::drawText(vg,r.x,r.y+height+8,16,Text(),hint.c_str());
            gfx::drawTextArgs(vg,r.x+r.w,r.y+height+8,16,NVG_ALIGN_RIGHT|NVG_ALIGN_TOP,Text(),"%u%%",maximum>0?unsigned(offset/maximum*100):100u);
            nvgRestore(vg);
        }
    }
};
}
