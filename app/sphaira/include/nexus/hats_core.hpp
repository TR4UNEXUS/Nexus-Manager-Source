// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/hats_shared/hats_release.hpp"
namespace nexus::hats::core {
inline std::string Lower(std::string text) { for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text; }
inline bool Digest(std::string_view text) {
    if(text.size()!=71||!text.starts_with("sha256:"))return false;
    return std::all_of(text.begin()+7,text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
inline bool Path(std::string_view path) {
    if(path.empty()||path.size()>240||path.front()=='/'||path.find_first_of("\\:\r\n")!=path.npos)return false;
    if(path.back()=='/')path.remove_suffix(1);
    while(!path.empty()) { auto end=path.find('/');auto p=path.substr(0,end);
        if(p.empty()||p=="."||p==".."||p.back()=='.'||p.back()==' ')return false;
        for(unsigned char c:p)if(c<32||c==127)return false;
        if(end==path.npos)break;path.remove_prefix(end+1);
    }return true;
}
inline std::string InstalledTag(std::string text) {
    constexpr std::string_view prefix="Nexus HATS - Atmosphere ";
    auto end=text.find_first_of("\r\n");text.resize(std::min(end,text.size()));
    if(!text.starts_with(prefix))return {};
    text.erase(0,prefix.size());
    if(text.empty()||text.size()>64)return {};
    for(char& c:text){if(c==' ')c='-';else if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return {};}
    return "v"+Lower(text);
}
}
