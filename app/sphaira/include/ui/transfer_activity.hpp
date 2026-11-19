// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/nexus_dialog.hpp"
#include "nexus/transfer_activity.hpp"
#include <cstdio>

namespace sphaira::ui::transferui {
struct InstallView {
    bool active{};
    std::string stage,note;
    s64 offset{},size{};
    bool weighted{};
    bool stopping{},slow{};
};
inline void Line(NVGcontext* vg,float x,float y,float width,float font,std::string text){
    nvgSave(vg);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgFontSize(vg,font);
    const auto fits=[&](const std::string& s){return nvgTextBounds(vg,0,0,s.c_str(),nullptr,nullptr)<=width;};
    if(!fits(text)){
        do{
            if(text.empty())break;
            auto at=text.size()-1;while(at&&(static_cast<unsigned char>(text[at])&0xc0)==0x80)--at;
            text.resize(at);
        }while(!fits(text+"..."));
        text+="...";
    }
    nvgIntersectScissor(vg,x,y,width,font+6);
    gfx::drawText(vg,x,y,font,nvgRGB(255,255,255),text.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgRestore(vg);
}
inline std::string Amount(std::uint64_t bytes){char b[48]{};std::snprintf(b,sizeof(b),"%.1f MiB",double(bytes)/1048576.0);return b;}
inline void Bar(NVGcontext* vg,float x,float y,float width,double fraction){
    gfx::drawRect(vg,x,y,width,5,nvgRGBA(150,155,210,65),2);
    gfx::drawRect(vg,x,y,width*std::clamp(fraction,0.0,1.0),5,violet::Accent(),2);
}
inline void Draw(NVGcontext* vg,const Vec4& rect,const nexus::transfer::activity::Snapshot& state,
                 const nexus::transfer::activity::Cursor& cursor,const InstallView& install={}){
    nvgSave(vg);nvgIntersectScissor(vg,rect.x,rect.y,rect.w,rect.h);violet::Card(vg,rect,false);
    const float x=rect.x+24,y=rect.y,w=rect.w-48;
    Line(vg,x,y+14,w,26,"PC transfer / Live activity");
    Line(vg,x,y+50,w,17,state.connection);
    Line(vg,x,y+77,w,16,"Uploaded: "+std::to_string(state.uploaded)+"    Installed: "+std::to_string(state.installed)+
         "    Waiting: "+std::to_string(state.queued)+"    Errors: "+std::to_string(state.errors));
    gfx::drawRect(vg,x,y+104,w,1,nvgRGBA(140,130,210,90));

    const auto& upload=state.upload;
    const auto& file=state.content;
    Line(vg,x,y+116,w,20,state.installing?"TO "+state.install_target+"  |  "+state.install_name:
         upload.id?"TO "+upload.target+"  |  "+upload.name:"Drop a package into Install to SD or Install to USB");
    char speed[64]{};std::snprintf(speed,sizeof(speed),"%.2f MiB/s",state.rate.bytes_per_second/1048576.0);
    auto uploadMetric=upload.id?"PC received: "+Amount(upload.bytes)+(upload.expected>=0?" / "+Amount(upload.expected):""):"Waiting for your PC";
    if(upload.active)uploadMetric+="    |    "+std::string(state.rate.sampled?speed:"Measuring speed...");
    uploadMetric+=state.streaming?"    |    DIRECT STREAM":"    |    "+std::string(state.installing?"Installing uploaded package":"Ready");
    Line(vg,x,y+146,w,17,uploadMetric);
    gfx::drawRect(vg,x,y+174,w,1,nvgRGBA(140,130,210,90));
    Line(vg,x,y+185,w,20,file.name.empty()?"NCA files will appear here as installation starts":"NCA  "+file.name);
    Line(vg,x,y+215,w,17,file.name.empty()?"File type, size and progress are shown for each content file.":
         "Type: "+file.kind+"    |    NCA size: "+Amount(file.size)+
         (file.packed&&file.packed!=file.size?"    |    In package: "+Amount(file.packed):""));
    std::string metrics;
    if(!file.name.empty()&&file.measuring){
        const double fraction=file.size?std::clamp(double(file.bytes)/double(file.size),0.0,1.0):0;
        metrics=Amount(file.bytes)+" / "+Amount(file.size)+"    |    "+std::to_string(unsigned(fraction*100))+"%";
        if(file.rate.sampled){char rate[64]{};std::snprintf(rate,sizeof(rate),"    |    %.2f MiB/s",file.rate.bytes_per_second/1048576.0);metrics+=rate;}
        Bar(vg,x,y+276,w,fraction);
    }else metrics=file.name.empty()?"Waiting for content":"Current operation: "+file.phase;
    Line(vg,x,y+244,w,17,metrics);
    auto stage=file.measuring?file.phase:(install.active?install.stage:state.stage);
    if(install.stopping)stage="Stopping safely. Waiting for storage to finish.";
    else if(install.slow)stage="Storage is taking longer than usual. Keep it connected.";
    if(stage.empty())stage=state.stopped?"Connection stopped":"Ready for an upload";
    Line(vg,x,y+289,w,16,stage);

    const auto end=cursor.End(state),begin=end>nexus::transfer::activity::Cursor::Rows?end-nexus::transfer::activity::Cursor::Rows:0;
    Line(vg,x,y+317,w,15,cursor.anchor?"RECENT ACTIVITY / Viewing earlier messages":"RECENT ACTIVITY / Live");
    for(std::size_t i=begin;i<end;++i){
        const auto& row=state.lines[i];char stamp[32]{};
        std::snprintf(stamp,sizeof(stamp),"%02llu:%02llu:%02llu",static_cast<unsigned long long>(row.seconds/3600),
            static_cast<unsigned long long>(row.seconds/60%60),static_cast<unsigned long long>(row.seconds%60));
        Line(vg,x,y+343+(i-begin)*22,91,15,stamp);
        Line(vg,x+100,y+343+(i-begin)*22,w-100,16,row.text);
    }
    Line(vg,x,y+502,w,14,std::string(gfx::getButton(::sphaira::Button::UP))+" "+gfx::getButton(::sphaira::Button::DOWN)+
         "  Scroll log     "+gfx::getButton(::sphaira::Button::Y)+"  Latest     Keep storage connected until the result appears.");
    nvgRestore(vg);
}
}
