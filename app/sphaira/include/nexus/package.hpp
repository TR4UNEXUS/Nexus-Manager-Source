// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace nexus::package {
enum class Kind { Unsupported, Nsp, Nsz, Xci, Xcz };
constexpr Kind Classify(std::string_view path) {
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
