// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace nexus::archive {
// A presentation-only canonical name. Never rename, write or hash source data.
inline std::string Canonical(std::string_view name) {
    if(name.size()!=36 && name.size()!=37 && name.size()!=41)return {};
    std::string out(name);
    for(auto& c:out)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    for(size_t i=0;i<32;++i)
        if(!((out[i]>='0' && out[i]<='9') || (out[i]>='a' && out[i]<='f')))return {};
    const auto suffix=std::string_view(out).substr(32);
    if(suffix!=".nca" && suffix!=".ncz" && suffix!=".cnmt.nca" && suffix!=".cnmt.ncz" && suffix!=".tik" && suffix!=".cert")return {};
    return out;
}
template<class Entries> bool Prepare(const Entries& input,Entries& output) {
    // A separate output is required. Fail without publishing a partial list.
    if(&input==&output)return false;
    output.clear();
    if(input.size()>65536)return false;
    size_t name_bytes=0;
    std::set<std::string> names;
    Entries next;next.reserve(input.size());
    for(const auto& entry:input) {
        if(entry.name.size()>16*1024*1024-name_bytes)return false;
        name_bytes+=entry.name.size();
        auto name=Canonical(entry.name);
        if(!name.empty() && !names.insert(name).second)return false;
        auto& copy=next.emplace_back(entry);
        if(!name.empty())copy.name=std::move(name);
    }
    output.swap(next);
    return true;
}
template<class Entries> const typename Entries::value_type* Certificate(const Entries& entries,std::string_view ticket) {
    auto name=Canonical(ticket);
    if(!name.ends_with(".tik"))return nullptr;
    name.replace(32,4,".cert");
    const typename Entries::value_type* result{};
    for(const auto& entry:entries) {
        if(Canonical(entry.name)!=name)continue;
        if(result)return nullptr; // Never choose the first ambiguous match.
        result=&entry;
    }
    return result;
}
}
