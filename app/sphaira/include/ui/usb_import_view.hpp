// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace sphaira::ui::usbimportview {
enum class State { Ready, AlreadyHere, Incomplete, MissingPermission };
enum class Kind { Game, Update, AddOn, AddOnUpdate, Unknown };
struct Item {
    std::uint64_t id{},application{};
    std::uint32_t version{};
    unsigned type{};
    State state{};
    Kind kind{Kind::Unknown};
    std::string name;
};
struct Preview {
    std::vector<bool> selected;
    std::vector<std::size_t> order;
    std::vector<std::string> family;
    std::size_t ready{},games{},updates{},addons{},already{},incomplete{},permissions{},older{};
};
inline std::uint64_t Family(const Item& item){return item.application?item.application:item.id;}
inline const char* KindName(Kind kind){
    switch(kind){case Kind::Game:return "Game";case Kind::Update:return "Update";
        case Kind::AddOn:return "DLC";case Kind::AddOnUpdate:return "DLC update";default:return "Other content";}
}
inline Preview Build(const std::vector<Item>& items,const std::vector<bool>* selection=nullptr){
    Preview out;
    std::map<std::pair<std::uint64_t,unsigned>,std::uint32_t> newest;
    std::map<std::uint64_t,std::string> names;
    for(const auto& item:items){
        if(item.id)newest[{item.id,item.type}]=std::max(newest[{item.id,item.type}],item.version);
        auto& name=names[Family(item)];
        if(name.empty()||item.kind==Kind::Game)name=item.name;
    }
    for(std::size_t i=0;i<items.size();++i){
        const auto& item=items[i];
        // A newer incomplete/missing-permission version still supersedes an
        // older Ready version. This mirrors the backend selection boundary.
        const bool selected=selection?(i<selection->size()&&(*selection)[i]):
            item.id&&item.state==State::Ready&&item.version==newest[{item.id,item.type}];
        out.selected.push_back(selected);out.order.push_back(i);out.family.push_back(names[Family(item)]);
        if(selected){
            ++out.ready;
            if(item.kind==Kind::Game)++out.games;
            else if(item.kind==Kind::Update)++out.updates;
            else if(item.kind==Kind::AddOn||item.kind==Kind::AddOnUpdate)++out.addons;
        }else switch(item.state){
            case State::Ready:if(item.id)++out.older;else ++out.incomplete;break;
            case State::AlreadyHere:++out.already;break;
            case State::Incomplete:++out.incomplete;break;
            case State::MissingPermission:++out.permissions;break;
        }
    }
    std::stable_sort(out.order.begin(),out.order.end(),[&](auto a,auto b){
        const auto& x=items[a];const auto& y=items[b];
        // Only explicit application IDs group updates/DLC. Do not infer a
        // parent from title-ID bit patterns when the metadata did not give one.
        return std::tuple(out.family[a],Family(x),x.kind,x.id,~x.version)
             < std::tuple(out.family[b],Family(y),y.kind,y.id,~y.version);
    });
    return out;
}
inline std::string ReadyBreakdown(const Preview& count){
    return "Games: "+std::to_string(count.games)+"  /  Updates: "+std::to_string(count.updates)+"  /  DLC items: "+std::to_string(count.addons);
}
inline std::string OtherBreakdown(const Preview& count){
    return std::to_string(count.already)+" already here  /  "+std::to_string(count.permissions)+" need permissions  /  "+std::to_string(count.incomplete)+" need checking"+
        (count.older?"  /  "+std::to_string(count.older)+" older versions":"");
}
// Raw metadata filenames remain in the saved report/detail panel, not list titles.
inline std::string OutcomeTitle(std::string_view text){
    auto line=std::string(text.substr(0,text.find('\n')));
    const auto start=line.find("Metadata ");
    if(start==std::string::npos)return line;
    const auto first=start+9,end=first+32;
    if(end>line.size())return line;
    for(auto i=first;i<end;++i)if(!((line[i]>='0'&&line[i]<='9')||(line[i]>='a'&&line[i]<='f')||(line[i]>='A'&&line[i]<='F')))return line;
    auto tail=end;
    if(line.compare(tail,2," [")==0){const auto reason=line.find(" - ",tail);tail=reason==std::string::npos?line.size():reason;}
    line.replace(start,tail-start,"Unrecognised content");
    return line;
}
template<class Measure>
inline std::string FitLineText(std::string_view text,float width,Measure measure){
    if(width<=0||text.empty())return {};
    if(measure(text)<=width)return std::string(text);
    constexpr std::string_view suffix="...";
    if(measure(suffix)>width)return {};
    std::vector<std::size_t> ends{0};
    for(std::size_t i=1;i<=text.size();++i)if(i==text.size()||(static_cast<unsigned char>(text[i])&0xc0)!=0x80)ends.push_back(i);
    std::size_t first=0,last=ends.size()-1;
    while(first<last){const auto middle=(first+last+1)/2;
        const auto value=std::string(text.substr(0,ends[middle]))+std::string(suffix);
        if(measure(value)<=width)first=middle;else last=middle-1;}
    return std::string(text.substr(0,ends[first]))+std::string(suffix);
}
}
