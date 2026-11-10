// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/usb_import_view.hpp"
#include <cassert>
#include <cstdio>
using namespace sphaira::ui::usbimportview;
int main(){
    auto empty=Build({});assert(empty.ready==0&&empty.order.empty());
    std::vector<Item> items{
        {11,10,1,2,State::Ready,Kind::Update,"Alpha update"},
        {21,20,1,3,State::Ready,Kind::AddOn,"Beta DLC"},
        {10,10,0,1,State::Ready,Kind::Game,"Alpha"},
        {11,10,2,2,State::MissingPermission,Kind::Update,"Alpha update"},
        {20,20,0,1,State::AlreadyHere,Kind::Game,"Beta"},
        {22,20,4,4,State::Ready,Kind::AddOnUpdate,"Beta DLC update"},
        {30,0,1,2,State::Incomplete,Kind::Update,"Unrelated"},
        {0,0,0,1,State::Ready,Kind::Unknown,"Invalid"}
    };
    const auto view=Build(items);
    assert(view.ready==3&&view.games==1&&view.updates==0&&view.addons==2);
    assert(view.already==1&&view.permissions==1&&view.incomplete==2&&view.older==1);
    assert(!view.selected[0]&&!view.selected[3]&&!view.selected[7]);
    assert(view.selected[1]&&view.selected[2]&&view.selected[5]);
    assert(view.family[0]=="Alpha"&&view.family[3]=="Alpha"&&view.family[1]=="Beta");
    assert(view.order[0]==2&&view.order[1]==3&&view.order[2]==0);
    assert(view.order[3]==4&&view.order[4]==1&&view.order[5]==5);
    assert(ReadyBreakdown(view)=="Games: 1  /  Updates: 0  /  DLC items: 2");
    assert(OtherBreakdown(view).find("1 older versions")!=std::string::npos);
    items[3].state=State::Ready;const auto updated=Build(items);
    assert(updated.ready==4&&updated.updates==1&&updated.selected[3]&&!updated.selected[0]);
    items[0].type=5;const auto separate_type=Build(items);
    assert(separate_type.selected[0]&&separate_type.selected[3]);
    const auto measure=[](std::string_view s){return float(s.size());};
    assert(FitLineText("abc",3,measure)=="abc");
    assert(FitLineText("abcdef",5,measure)=="ab...");
    assert(FitLineText("abcdef",2,measure).empty());
    assert(FitLineText("abcdef",0,measure).empty());
    assert(FitLineText("\xC3\xA9\xC3\xA9\xC3\xA9",5,measure)=="\xC3\xA9...");
    assert(OutcomeTitle("Skipped: Metadata d6560171b1ede6665d8294c77c247d51 [0000000000000000] v0 - unsupported") == "Skipped: Unrecognised content - unsupported");
    assert(OutcomeTitle("Metadata d6560171b1ede6665d8294c77c247d51")=="Unrecognised content");
    assert(OutcomeTitle("Metadata game title") == "Metadata game title");
    assert(OutcomeTitle("Imported: Game [123] v4\nfull details")=="Imported: Game [123] v4");
    std::puts("PASS USB import view: newest-version safety, explicit family grouping, typed ready counts, missing permissions, invalid IDs and stable browse order");
}
