// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace nexus::package {
enum class Kind { Unsupported, Nsp, Nsz, Xci, Xcz };
constexpr bool EqualAscii(std::string_view a, std::string_view b) {
    if(a.size()!=b.size())return false;
    for(unsigned i=0;i<a.size();++i) {
        const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+32):c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
struct SplitXci { unsigned digits{}; std::size_t prefix{}; unsigned index{}; explicit constexpr operator bool()const{return digits!=0;} };
constexpr SplitXci SplitPart(std::string_view path) {
    if(path.size()>=4 && EqualAscii(path.substr(path.size()-4,3),".xc") && path.back()>='0' && path.back()<='9')
        return {1,path.size()-1,unsigned(path.back()-'0')};
    if(path.size()>=7 && EqualAscii(path.substr(path.size()-7,5),".xci.") &&
       path[path.size()-2]>='0' && path[path.size()-2]<='9' && path.back()>='0' && path.back()<='9')
        return {2,path.size()-2,unsigned(path[path.size()-2]-'0')*10+unsigned(path.back()-'0')};
    return {};
}
constexpr Kind Classify(std::string_view path) {
    if(const auto split=SplitPart(path); split && split.index==0)return Kind::Xci;
    const auto slash=path.find_last_of("/\\");
    const auto dot=path.find_last_of('.');
    if(dot==path.npos || (slash!=path.npos && dot<slash) || path.size()-dot!=4) return Kind::Unsupported;
    char ext[3]{};
    for(unsigned i=0;i<3;++i) {
        const char c=path[dot+1+i];
        ext[i]=(c>='A' && c<='Z')?c+('a'-'A'):c;
    }
    const std::string_view value(ext,3);
    if(value=="nsp") return Kind::Nsp;
    if(value=="nsz") return Kind::Nsz;
    if(value=="xci") return Kind::Xci;
    if(value=="xcz") return Kind::Xcz;
    return Kind::Unsupported;
}
constexpr bool Supported(std::string_view path) {return Classify(path)!=Kind::Unsupported;}
} // namespace nexus::package
