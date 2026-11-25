// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/hats_core.hpp"
#include <charconv>

namespace nexus::hats {
inline constexpr char IdentityPath[]="/atmosphere/nexus-pack.json";
struct Identity {
    unsigned sequence{};
    std::string version,edition,atmosphere,tag;
};
// NX is one release sequence shared by every edition. It does not reset when
// the Atmosphere version changes, and is independent of Manager's app version.
inline unsigned Sequence(std::string_view text) {
    if(text.starts_with('v')||text.starts_with('V'))text.remove_prefix(1);
    if(!text.starts_with("NX")&&!text.starts_with("nx"))return 0;
    text.remove_prefix(2);
    if(text.empty()||text.size()>7||text.front()=='0')return 0;
    unsigned value{};const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),value);
    return ec==std::errc{}&&end==text.data()+text.size()?value:0;
}
inline unsigned EditionCode(std::string_view edition) {
    if(edition=="standard-no-hoc")return 1;
    if(edition=="standard-hoc")return 2;
    if(edition=="nexus-themed-hoc")return 3;
    return 0;
}
inline bool ReadIdentity(std::string_view json,Identity& out) {
    out={};if(json.empty()||json.size()>4096)return false;
    auto* doc=yyjson_read(json.data(),json.size(),0);if(!doc)return false;
    struct Cleanup{yyjson_doc* doc;~Cleanup(){yyjson_doc_free(doc);}} cleanup{doc};
    auto* root=yyjson_doc_get_root(doc);if(!yyjson_is_obj(root))return false;
    // Reject duplicate fields, rather than accepting ambiguous identities.
    std::vector<std::string> keys;size_t index,max;yyjson_val *key,*val;
    yyjson_obj_foreach(root,index,max,key,val){
        std::string name(yyjson_get_str(key),yyjson_get_len(key));
        if(std::find(keys.begin(),keys.end(),name)!=keys.end())return false;keys.push_back(name);
    }
    auto text=[&](const char* name)->std::string{
        auto* value=yyjson_obj_get(root,name);return yyjson_is_str(value)?std::string(yyjson_get_str(value),yyjson_get_len(value)):std::string{};
    };
    auto* schema=yyjson_obj_get(root,"schema");auto* sequence=yyjson_obj_get(root,"sequence");
    Identity id;id.version=text("version");id.edition=text("edition");id.atmosphere=text("atmosphere");id.tag=text("release_tag");
    const auto number=Sequence(id.version);
    if(!yyjson_is_uint(schema)||yyjson_get_uint(schema)!=1||text("product")!="Nexus HATS"||
       !yyjson_is_uint(sequence)||!number||yyjson_get_uint(sequence)!=number||
       id.version!="NX"+std::to_string(number)||id.tag!="vNX"+std::to_string(number)||
       !EditionCode(id.edition)||!sphaira::ui::menu::hats::ParseAppVersion(id.atmosphere))return false;
    id.sequence=number;out=std::move(id);return true;
}
inline bool MatchesPack(const Identity& id,std::string_view tag,std::string_view filename) {
    return id.sequence&&id.tag==tag&&core::Lower(std::string(filename))==
        "nexus-"+core::Lower(id.version)+"-"+id.edition+".zip";
}
inline std::string ReleaseLabel(std::string_view tag){const auto n=Sequence(tag);return n?"NX"+std::to_string(n):std::string(tag);}
}
