// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
namespace nexus::progressui {
struct Rect {float x{},y{},w{},h{};};
struct Layout {
    Rect column,cover,action,title,stage,status,advice,bar,percent,metric,note;
    float footer_y{},spinner_x{},spinner_y{};
    bool centered{};
};
inline Layout Make(Rect panel,bool cover,bool /*network*/){
    Layout out{};out.centered=!cover;out.footer_y=panel.y+panel.h-60;
    const float inset=cover?240.f:32.f;
    out.column={panel.x+inset,panel.y+70,std::max(0.f,panel.w-inset-32),188};
    const auto row=[&](float y,float h){return Rect{out.column.x,panel.y+y,out.column.w,h};};
    out.cover={panel.x+32,panel.y+76,176,176};
    out.action=row(70,28);out.title=row(106,104);out.stage=row(222,32);
    out.status={panel.x+32,panel.y+270,panel.w-64,26};
    out.advice={panel.x+32,panel.y+300,panel.w-64,26};
    out.bar={panel.x+32,out.footer_y-76,panel.w-132,12};
    out.percent={out.bar.x+out.bar.w+16,out.bar.y-7,52,26};
    out.spinner_x=out.bar.x;out.spinner_y=out.bar.y+6;
    out.metric={panel.x+32,out.bar.y+28,panel.w-64,32};
    return out;
}
inline float TextX(Rect row,float measured_width,bool centered){return row.x+(centered?std::max(0.f,(row.w-measured_width)/2):0);}
inline bool Within(Rect inner,Rect outer){return inner.w>=0&&inner.h>=0&&inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;}
}
