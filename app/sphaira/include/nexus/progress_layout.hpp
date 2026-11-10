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
inline Layout Make(Rect panel,bool cover,bool network){
    Layout out{};out.centered=!cover;out.footer_y=panel.y+panel.h-60;
    const float inset=cover?164.f:32.f;
    out.column={panel.x+inset,panel.y+60,std::max(0.f,panel.w-inset-32),out.footer_y-panel.y-60};
    const auto row=[&](float y,float h){return Rect{out.column.x,panel.y+y,out.column.w,h};};
    out.cover={panel.x+32,panel.y+62,108,108};
    out.action=row(60,38);out.title=row(109,34);out.stage=row(153,30);
    out.status=row(203,44);out.advice=row(250,38);
    out.bar={out.column.x+44,out.footer_y-96,std::max(0.f,out.column.w-108),12};
    out.percent={out.bar.x+out.bar.w+12,out.bar.y-7,52,26};
    out.spinner_x=out.bar.x-25;out.spinner_y=out.bar.y+out.bar.h/2;
    out.metric={out.column.x,out.bar.y+28,out.column.w,network?28.f:58.f};
    out.note=network?Rect{out.column.x,out.bar.y+60,out.column.w,28}:Rect{};
    return out;
}
inline float TextX(Rect row,float measured_width,bool centered){
    return row.x+(centered?std::max(0.f,(row.w-measured_width)/2):0);
}
inline bool Within(Rect inner,Rect outer){
    return inner.w>=0&&inner.h>=0&&inner.x>=outer.x&&inner.y>=outer.y&&
        inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;
}
}
