// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/release.hpp"
#include <yyjson.h>
#include <cstring>
#include <algorithm>
namespace nexus::updates {
std::uint32_t VersionCode(std::string_view s){
    if(s.starts_with("v"))s.remove_prefix(1);
    if(s.ends_with("-preview"))s.remove_suffix(8);
    std::uint32_t value=0;
    for(unsigned part=0;part<3;++part){
        auto dot=s.find('.');auto field=s.substr(0,dot);
        if(field.empty() || field.size()>3 || (field.size()>1 && field[0]=='0'))return 0;
        unsigned v=0;for(char c:field){if(c<'0'||c>'9')return 0;v=v*10+c-'0';}
        value=value*1000+v;
        if(part==2){if(dot!=s.npos)return 0;}
        else {if(dot==s.npos)return 0;s.remove_prefix(dot+1);}
    }
    return value;
}
namespace {
std::string String(yyjson_val* object,const char* key){
    auto* v=yyjson_obj_get(object,key);
    return yyjson_is_str(v)?std::string(yyjson_get_str(v),yyjson_get_len(v)):std::string{};
}
bool Digest(std::string_view value){
    if(value.size()!=71 || !value.starts_with("sha256:"))return false;
    for(char c:value.substr(7))if(!((c>='0' && c<='9')||(c>='a' && c<='f')))return false;
    return true;
}
}
bool ValidRelease(const Release& r){
    if(r.kind!=Artifact::Nro && r.kind!=Artifact::Nsp)return false;
    if(!VersionCode(r.version) || !r.tag.starts_with("v") || VersionCode(r.tag)!=VersionCode(r.version))return false;
    if(r.version.starts_with("v") || r.version.ends_with("-preview"))return false;
    if(r.tag!="v"+r.version && r.tag!="v"+r.version+"-preview")return false;
    return r.size>=65536 && r.size<=64*1024*1024 && Digest("sha256:"+r.sha256) &&
        r.url==std::string(Repository)+"/releases/download/"+r.tag+"/"+AssetName(r.kind);
}
bool SelectRelease(std::string_view json,Release& out,Artifact kind){
    out={};if(json.size()>2*1024*1024)return false;
    auto* doc=yyjson_read(json.data(),json.size(),0);if(!doc)return false;
    auto* root=yyjson_doc_get_root(doc);
    if(!yyjson_is_arr(root)){yyjson_doc_free(doc);return false;}
    size_t index,max;yyjson_val* item;std::uint32_t best=0;
    yyjson_arr_foreach(root,index,max,item){
        if(!yyjson_is_bool(yyjson_obj_get(item,"draft")) || yyjson_get_bool(yyjson_obj_get(item,"draft")))continue;
        if(!yyjson_is_bool(yyjson_obj_get(item,"prerelease")))continue;
        auto tag=String(item,"tag_name");auto code=VersionCode(tag);
        if(!code || code<=best || !tag.starts_with("v"))continue;
        auto* assets=yyjson_obj_get(item,"assets");size_t ai,am;yyjson_val* asset;
        yyjson_arr_foreach(assets,ai,am,asset){
            if(String(asset,"name")!=AssetName(kind) || String(asset,"state")!="uploaded")continue;
            const auto digest=String(asset,"digest"),url=String(asset,"browser_download_url");
            auto* size=yyjson_obj_get(asset,"size");
            if(!Digest(digest)||!yyjson_is_uint(size))continue;
            auto bytes=yyjson_get_uint(size);
            if(bytes<65536 || bytes>64*1024*1024)continue;
            if(url!=std::string(Repository)+"/releases/download/"+tag+"/"+AssetName(kind))continue;
            auto version=tag.substr(1);if(version.ends_with("-preview"))version.resize(version.size()-8);
            Release candidate{version,tag,url,digest.substr(7),bytes,yyjson_get_bool(yyjson_obj_get(item,"prerelease")),kind};
            if(!ValidRelease(candidate))continue;
            out=std::move(candidate);
            best=code;break;
        }
    }
    yyjson_doc_free(doc);return best!=0;
}
bool ValidNro(std::string_view bytes,std::string_view version){
    auto fits=[&](std::uint64_t off,std::uint64_t size){return off<=bytes.size() && size<=bytes.size()-off;};
    auto read=[&](std::uint64_t off,unsigned count){std::uint64_t value=0;if(fits(off,count))for(unsigned i=0;i<count;++i)value|=std::uint64_t(static_cast<unsigned char>(bytes[off+i]))<<(i*8);return value;};
    if(!fits(0,0x80)||bytes.substr(0x10,4)!="NRO0")return false;
    const auto aset=read(0x18,4);
    if(!fits(aset,56)||bytes.substr(aset,4)!="ASET")return false;
    for(unsigned pair=0;pair<3;++pair){
        auto off=read(aset+8+16*pair,8),size=read(aset+16+16*pair,8);
        if(off<56 || !size || off>bytes.size()-aset || !fits(aset+off,size))return false;
    }
    const auto offset=aset+read(aset+24,8),size=read(aset+32,8);
    if(size!=0x4000)return false;
    auto name=bytes.substr(offset,512);name=name.substr(0,name.find('\0'));
    auto ver=bytes.substr(offset+0x3060,16);ver=ver.substr(0,ver.find('\0'));
    return name=="Nexus Manager" && ver==version;
}
bool ValidNsp(std::string_view bytes){
    // Download structural check. Package paths are never extracted or executed.
    // Exact artifact integrity comes from GitHub's HTTPS release-asset SHA-256;
    // the publisher verifies native NSO/NPDM/NACP/title identity, and the
    // dedicated installer checks the Manager title/content before publication.
    if(bytes.size()<16 || bytes.substr(0,4)!="PFS0")return false;
    auto read=[&](size_t off,unsigned count){std::uint64_t v=0;for(unsigned i=0;i<count;++i)v|=std::uint64_t(static_cast<unsigned char>(bytes[off+i]))<<(8*i);return v;};
    const auto count=read(4,4),strings=read(8,4);
    if(count!=3 || !strings || strings>4096 || read(12,4))return false;
    const std::uint64_t names=16+count*24,data=names+strings;
    if(data>bytes.size())return false;
    std::string_view seen[3];std::uint64_t ends=0;unsigned meta=0;
    for(unsigned i=0;i<count;++i){
        const auto entry=16+i*24;
        const auto off=read(entry,8),size=read(entry+8,8),name=read(entry+16,4);
        if(read(entry+20,4) || off!=ends || !size || off>bytes.size()-data || size>bytes.size()-data-off || name>=strings)return false;
        auto text=bytes.substr(names+name,strings-name);auto zero=text.find('\0');if(zero==text.npos)return false;text=text.substr(0,zero);
        if(text.size()!=36 && text.size()!=41)return false;
        for(char c:text.substr(0,32))if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        if(text.substr(32)==".cnmt.nca")++meta;else if(text.substr(32)!=".nca")return false;
        for(unsigned j=0;j<i;++j)if(seen[j]==text)return false;
        seen[i]=text;ends=off+size;
    }
    return meta==1 && ends==bytes.size()-data;
}
}
