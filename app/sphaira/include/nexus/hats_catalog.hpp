#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/hats_identity.hpp"
namespace nexus::hats {
using Pack=sphaira::ui::menu::hats::ReleaseEntry;
struct Release {std::string tag,installed;std::vector<Pack> packs;bool newer{};};
inline bool ValidPack(const Pack& p) {
    constexpr std::string_view base="https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases/download/";
    return !p.prerelease&&p.size>1024&&p.size<=128*1024*1024&&p.asset_id&&core::Digest(p.asset_digest)&&
        core::Path(p.asset_name)&&p.asset_name.find('/')==p.asset_name.npos&&core::Path(p.tag_name)&&p.tag_name.find('/')==p.tag_name.npos&&
        p.download_url==std::string(base)+p.tag_name+"/"+p.asset_name;
}
inline bool Parse(std::string_view json,std::string_view installed,Release& out) {
    out={};auto* doc=yyjson_read(json.data(),json.size(),0);if(!doc)return false;struct Cleanup { yyjson_doc* doc; ~Cleanup(){yyjson_doc_free(doc);} } cleanup{doc};
    auto rows=sphaira::ui::menu::hats::ParsePackReleases(yyjson_doc_get_root(doc));std::vector<std::string> tags;
    for(const auto& p:rows)if(ValidPack(p)){
        if(std::find(tags.begin(),tags.end(),p.tag_name)==tags.end())tags.push_back(p.tag_name);
        if(out.tag.empty())out.tag=p.tag_name;
        if(p.tag_name==out.tag)out.packs.push_back(p);
    }
    // GitHub can reorder older releases when edited. NX releases are ordered
    // by the shared integer, never by edition, dates or string comparison.
    unsigned latest{};
    for(const auto& p:rows)if(ValidPack(p)&&Sequence(p.tag_name)>latest){latest=Sequence(p.tag_name);out.tag=p.tag_name;}
    if(latest){out.packs.clear();for(const auto& p:rows)if(ValidPack(p)&&p.tag_name==out.tag)out.packs.push_back(p);}
    out.installed=std::string(installed);
    const auto current=std::find(tags.begin(),tags.end(),out.installed);
    // Tags are release identities, not numeric versions. Unknown/custom builds
    // stay available for manual updates without a misleading startup prompt.
    const auto installed_sequence=Sequence(installed);
    out.newer=installed_sequence?latest>installed_sequence:
        (current!=tags.end()&&(latest||*current!=out.tag));
    return !out.packs.empty();
}
}
