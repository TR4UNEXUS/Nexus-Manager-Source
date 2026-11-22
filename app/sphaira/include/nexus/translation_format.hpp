// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace nexus::languages {
inline std::string FormatSignature(std::string_view text){
    std::string out;
    for(std::size_t i=0;i<text.size();++i){
        if(text[i]!='%')continue;
        const auto start=i;
        auto p=i+1;
        while(p<text.size() && std::string_view("0123456789$-+#0 '.*hljztL").find(text[p])!=std::string_view::npos)++p;
        if(p<text.size() && std::string_view("diuoxXfFeEgGaAcspn%").find(text[p])!=std::string_view::npos){
            out.append(text.substr(start,p-start+1));out+='\x1f';i=p;
        }
    }
    return out;
}
inline bool FormatCompatible(std::string_view source,std::string_view translated){
    return FormatSignature(source)==FormatSignature(translated);
}
}
