// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/network_browser.hpp"
#include "ui/nexus_violet.hpp"
#include "ui/nexus_icons.hpp"
#include "ui/nvg_util.hpp"
#include <utility>

namespace sphaira::ui::menu::network {
namespace {
namespace view=nexusview;
struct IconPainter {
    NVGcontext* vg;
    void Path(std::initializer_list<view::Point> points,bool close){nvgBeginPath(vg);bool first=true;for(auto p:points){if(first)nvgMoveTo(vg,p.x,p.y);else nvgLineTo(vg,p.x,p.y);first=false;}if(close)nvgClosePath(vg);nvgStroke(vg);}
    void Rect(float x,float y,float w,float h,float radius){nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,radius);nvgStroke(vg);}
    void Circle(float x,float y,float radius){nvgBeginPath(vg);nvgCircle(vg,x,y,radius);nvgStroke(vg);}
};
void Icon(NVGcontext* vg,nexus::network::browser::Kind kind,float x,float y){
    nvgSave(vg);nvgTranslate(vg,x,y);nvgScale(vg,40.f/96,40.f/96);
    nvgStrokeColor(vg,nvgRGB(255,255,255));nvgStrokeWidth(vg,3.2f);nvgLineCap(vg,NVG_ROUND);nvgLineJoin(vg,NVG_ROUND);
    IconPainter painter{vg};view::PaintIcon(painter,kind==nexus::network::browser::Kind::Folder?view::MenuIcon::Folder:kind==nexus::network::browser::Kind::Action?view::MenuIcon::HomeInstall:view::MenuIcon::MoreCartridge);nvgRestore(vg);
}
}
Browser::Browser(std::shared_ptr<nexus::network::browser::Model> state):MenuBase("Network browser",MenuFlag_None),model(std::move(state)){
    SetupActions();
    SetAction(Button::UP,Action{[this]{model->Move(false);}});
    SetAction(Button::DOWN,Action{[this]{model->Move(true);}});
    SetAction(Button::LEFT,Action{[this]{model->Move(false,nexus::network::browser::PageSize);}});
    SetAction(Button::RIGHT,Action{[this]{model->Move(true,nexus::network::browser::PageSize);}});
}
void Browser::SetupActions(){
    SetAction(Button::A,Action{model->rows.empty()?"Select":model->rows[model->selected].kind==nexus::network::browser::Kind::Folder?"Open folder":"Select",[this]{model->Activate();}});
    SetAction(Button::B,Action{model->back?"Parent folder":"Back",[this]{auto back=model->back;if(back)back();else SetPop();}});
    if(model->install_selected){
        if(model->selected<model->rows.size()&&!model->rows[model->selected].selection_path.empty())
            SetAction(Button::Y,Action{model->IsMarked(model->rows[model->selected])?"Unmark":model->marked.size()>=nexus::network::browser::MaxSelection?"64 file limit":"Mark file",[this]{model->Toggle();}});
        else RemoveAction(Button::Y);
        SetAction(Button::X,Action{"Install selected",[this]{model->InstallSelected();}});
        SetAction(Button::R,Action{"Refresh",[this]{auto refresh=model->refresh;if(refresh)refresh();}});
    }else{
        RemoveAction(Button::Y);RemoveAction(Button::R);
        SetAction(Button::X,Action{"Refresh",[this]{auto refresh=model->refresh;if(refresh)refresh();}});
    }
    last_selected=model->selected;last_revision=model->revision;
}
void Browser::Update(Controller* controller,TouchInfo* touch){
    if(last_selected!=model->selected||last_revision!=model->revision)SetupActions();
    MenuBase::Update(controller,touch);
    if(!HasFocus()||ShouldPop())return;
    if(touch->is_clicked){const auto start=model->PageStart();for(size_t i=start;i<std::min(model->rows.size(),start+nexus::network::browser::PageSize);++i){
        const auto y=174+(i-start)*60.f;
        if(touch->in_range({54,y,1172,56})){
            if(model->install_selected&&!model->rows[i].selection_path.empty()&&touch->in_range({112,y+6,40,44})){model->selected=i;model->Toggle();}
            else if(model->selected==i)model->Activate();else model->selected=i;break;
        }
    }}
    if(last_selected!=model->selected||last_revision!=model->revision)SetupActions();
    SetSubHeading(std::to_string(model->rows.empty()?0:model->selected+1)+" / "+std::to_string(model->rows.size()));
}
void Browser::Draw(NVGcontext* vg,Theme* theme){
    MenuBase::Draw(vg,theme);
    const auto white=nvgRGB(255,255,255);
    violet::Card(vg,{42,106,1196,533},false);
    title_text.Draw(vg,HasFocus(),54,113,model->install_selected?950:1172,26,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,white,model->title);
    if(model->install_selected){const auto count=std::to_string(model->marked.size())+" selected";gfx::drawText(vg,1206,116,21,white,count.c_str(),NVG_ALIGN_RIGHT|NVG_ALIGN_TOP);}
    path_text.Draw(vg,HasFocus(),54,146,1172,17,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,white,model->location);
    const auto start=model->PageStart();
    for(size_t i=start;i<std::min(model->rows.size(),start+nexus::network::browser::PageSize);++i){
        const auto& row=model->rows[i];const auto y=174+(i-start)*60.f;
        violet::Card(vg,{54,y,1172,56},model->selected==i,true);Icon(vg,row.kind,68,y+8);
        if(!row.selection_path.empty()){
            nvgSave(vg);nvgStrokeColor(vg,white);nvgStrokeWidth(vg,2);nvgBeginPath(vg);nvgRoundedRect(vg,120,y+18,20,20,4);nvgStroke(vg);
            if(model->IsMarked(row)){nvgBeginPath(vg);nvgMoveTo(vg,124,y+28);nvgLineTo(vg,128,y+32);nvgLineTo(vg,136,y+23);nvgStroke(vg);}nvgRestore(vg);
        }
        const auto filename_x=model->install_selected?156.f:120.f;
        selected_text.Draw(vg,HasFocus()&&i==model->selected,filename_x,y+17,970-filename_x,21,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,white,row.name);
        nvgSave(vg);nvgScissor(vg,990,y+8,216,40);gfx::drawText(vg,1206,y+19,17,white,row.detail.c_str(),NVG_ALIGN_RIGHT|NVG_ALIGN_TOP);nvgRestore(vg);
    }
    if(model->rows.empty())gfx::drawText(vg,640,320,23,white,model->empty.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_TOP);
    help_text.Draw(vg,HasFocus(),54,610,1172,16,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,white,model->help);
}
}
