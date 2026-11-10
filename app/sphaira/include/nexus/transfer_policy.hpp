// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include "nexus/package.hpp"
namespace nexus::transfer {
inline constexpr char Root[]="/switch/nexusmanager/transfers";
enum class Use {FilesSd,FilesUsb,InstallSd,InstallUsb,SaveDownloads,SaveUploads,GamesSd,GamesUsb,GamesInternal};
inline bool Exporting(Use u){return u==Use::GamesSd || u==Use::GamesUsb || u==Use::GamesInternal;}
inline bool Usb(Use u){return u==Use::FilesUsb || u==Use::InstallUsb;}
inline bool Installing(Use u){return u==Use::InstallSd || u==Use::InstallUsb;}
// Stable, distinct roots: libhaze indexes objects by their complete path.
// Use the same names as FTP root folders so destinations mean the same thing.
inline const char* VolumeName(Use u){switch(u){
case Use::FilesSd:return "1 - SD Files";case Use::FilesUsb:return "2 - USB Files";
case Use::InstallSd:return "3 - SD Install";case Use::InstallUsb:return "4 - USB Install";
case Use::GamesSd:return "5 - Games on SD";case Use::GamesUsb:return "6 - Games on USB";
case Use::GamesInternal:return "7 - Games in System Memory";
case Use::SaveDownloads:return "8 - Save Backups";case Use::SaveUploads:return "9 - Save Uploads";}return nullptr;}
inline const char* Label(Use u){switch(u){
case Use::FilesSd:return "SD file transfer";case Use::FilesUsb:return "USB file transfer";
case Use::InstallSd:return "Install games to SD";case Use::InstallUsb:return "Install games to USB";
case Use::SaveDownloads:return "Download save backups";case Use::SaveUploads:return "Upload save backups";
case Use::GamesSd:return "Installed games on SD";case Use::GamesUsb:return "Installed games on USB";case Use::GamesInternal:return "Installed games in system memory";}return "Invalid";}
inline const char* Base(Use u){switch(u){
case Use::FilesSd:case Use::FilesUsb:return Root;
case Use::InstallSd:case Use::InstallUsb:return "/switch/nexusmanager/transfers/install";
case Use::SaveDownloads:return "/switch/nexusmanager/save-backups";
case Use::SaveUploads:return "/switch/nexusmanager/save-imports";
case Use::GamesSd:case Use::GamesUsb:case Use::GamesInternal:return "/installed-games";}return nullptr;}
inline bool IsPartial(std::string_view s){
    if(s.size()<8)return false;
    s.remove_prefix(s.size()-8);
    constexpr std::string_view suffix=".partial";
    for(size_t i=0;i<s.size();++i){char c=s[i];if(c>='A'&&c<='Z')c+=32;if(c!=suffix[i])return false;}return true;
}
inline bool CanWrite(std::int64_t offset,std::int64_t expected,std::int64_t requested,std::uint64_t count,bool failed){
    return !failed && offset>=0 && requested==offset && count<=std::uint64_t(INT64_MAX-offset) &&
        (expected<0 || (offset<=expected && count<=std::uint64_t(expected-offset)));
}
inline bool CanPublish(std::int64_t offset,std::int64_t expected,bool complete,bool failed){
    return complete && !failed && offset>=0 && (expected<0 || offset==expected);
}
// A protocol path is never a native/device path. Reject, do not normalize away,
// traversal, alternate separators and FAT aliases before resolving it.
inline bool Resolve(std::string_view input,std::string& output,bool mutation=false) {
    if(input.empty() || input.size()>600 || input.front()!='/')return false;
    if(input=="/"){if(mutation)return false;output=Root;return true;}
    if(input.back()=='/')input.remove_suffix(1);
    size_t pos=1;
    while(pos<input.size()){
        auto end=input.find('/',pos);if(end==input.npos)end=input.size();
        auto part=input.substr(pos,end-pos);
        if(part.empty() || part=="." || part==".." || part.size()>240 || part.back()=='.' || part.back()==' ')return false;
        for(unsigned char c:part)if(c<32 || c==127 || c==':' || c=='\\' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
        if(IsPartial(part))return false;
        pos=end+1;
    }
    output=std::string(Root)+std::string(input);return true;
}
inline bool ResolveFor(Use use,std::string_view input,std::string& output,bool mutation=false,bool directory=false){
    const auto* base=Base(use);if(!base || (mutation && (use==Use::SaveDownloads || Exporting(use))))return false;
    std::string checked;if(!Resolve(input,checked,mutation))return false;
    auto relative=std::string_view(checked).substr(sizeof(Root)-1);
    if(Installing(use) && !relative.empty() && (directory || relative.find('/',1)!=relative.npos || !nexus::package::Supported(relative)))return false;
    output=std::string(base)+std::string(relative);
    return output.size()<760;
}
inline bool ResolveVolume(Use use,std::string_view input,std::string& output,bool mutation=false,bool directory=false){
    const auto* name=VolumeName(use);if(!name)return false;
    const std::string root=std::string("/")+name;
    if(input==root || input==root+"/")return ResolveFor(use,"/",output,mutation,directory);
    if(!input.starts_with(root+"/"))return false;
    return ResolveFor(use,input.substr(root.size()),output,mutation,directory);
}
inline bool CartridgeGeometry(std::uint64_t normal,std::uint64_t secure,std::uint32_t last,std::uint64_t& trimmed){
    constexpr std::uint64_t Limit=64ULL*1024*1024*1024;
    trimmed=(std::uint64_t(last)+1)*512;
    return normal>=0x8000 && secure>0 && normal%512==0 && secure%512==0 && normal<=Limit && secure<=Limit-normal && trimmed>=normal && trimmed<=normal+secure;
}
}
