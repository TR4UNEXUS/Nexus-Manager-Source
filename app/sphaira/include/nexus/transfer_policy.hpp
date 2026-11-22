// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <string>
#include <string_view>
#include <cstdint>
#include <utility>
#include <algorithm>
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
case Use::FilesSd:return "3 - SD Card Files"_nx;case Use::FilesUsb:return "4 - USB Transfer Files"_nx;
case Use::InstallSd:return "1 - Install to SD"_nx;case Use::InstallUsb:return "2 - Install to USB"_nx;
case Use::GamesSd:return "5 - Games on SD"_nx;case Use::GamesUsb:return "6 - Games on USB"_nx;
case Use::GamesInternal:return "7 - Games in System Memory"_nx;
case Use::SaveDownloads:return "6 - Save Backups"_nx;case Use::SaveUploads:return "7 - Upload Save Backups"_nx;}return nullptr;}
inline constexpr char FtpGamesName[]="5 - Export Installed Games";
inline constexpr char InstallGuideName[]="START HERE - Install games.txt";
// Install folders are drop boxes. A repeated FTP upload gets its own package;
// existing packages, unfinished uploads and queued installs are never replaced.
inline std::string RetryUploadPath(std::string_view original,unsigned attempt){
    if(!attempt)return std::string(original);
    const auto slash=original.find_last_of('/');
    const auto leaf=original.substr(slash==original.npos?0:slash+1);
    const auto dot=leaf.find_last_of('.');
    if(dot==leaf.npos)return {};
    const auto extension=leaf.substr(dot);
    const auto suffix=" (upload "+std::to_string(attempt+1)+")";
    if(extension.size()+suffix.size()>=240)return {};
    auto length=std::min(dot,240-extension.size()-suffix.size());
    while(length&&(static_cast<unsigned char>(leaf[length])&0xc0)==0x80)--length;
    return std::string(original.substr(0,slash==original.npos?0:slash+1))+std::string(leaf.substr(0,length))+suffix+std::string(extension);
}
inline const char* Label(Use u){switch(u){
case Use::FilesSd:return "SD file transfer"_nx;case Use::FilesUsb:return "USB file transfer"_nx;
case Use::InstallSd:return "Install games to SD"_nx;case Use::InstallUsb:return "Install games to USB"_nx;
case Use::SaveDownloads:return "Download save backups"_nx;case Use::SaveUploads:return "Upload save backups"_nx;
case Use::GamesSd:return "Installed games on SD"_nx;case Use::GamesUsb:return "Installed games on USB"_nx;case Use::GamesInternal:return "Installed games in system memory"_nx;}return "Invalid";}
inline const char* Base(Use u){switch(u){
case Use::FilesSd:case Use::FilesUsb:return Root;
case Use::InstallSd:case Use::InstallUsb:return "/switch/nexusmanager/transfers/install";
case Use::SaveDownloads:return "/switch/nexusmanager/save-backups";
case Use::SaveUploads:return "/switch/nexusmanager/save-imports";
case Use::GamesSd:case Use::GamesUsb:case Use::GamesInternal:return "/installed-games";}return nullptr;}
inline bool IsInstallGuide(Use use,std::string_view path){return Installing(use)&&path==std::string(Base(use))+"/"+InstallGuideName;}
inline std::string InstallGuide(Use use,bool ready,std::string_view problem={}){
    const bool usb=use==Use::InstallUsb;
    std::string text="NEXUS MANAGER - INSTALL TO "_nx+std::string(usb?"USB DRIVE"_nx:"SD CARD"_nx)+"\r\n\r\n";
    if(!ready)text+="USB INSTALL IS NOT READY\r\n"_nx+std::string(problem)+"\r\nConnect your USB drive, stop PC Transfer on the Switch, then open it again.\r\nUploads are blocked until USB is ready. They will never be redirected to SD.\r\n\r\n"_nx;
    text+="1. Drag and drop your NSP, NSZ, XCI or XCZ files into this folder.\r\n2. Keep Nexus Manager open and the storage connected.\r\n3. Wait for installation to finish on the Switch before disconnecting.\r\n\r\nUse binary transfers and one simultaneous upload in your FTP app.\r\nUpload the original filename directly; do not use temporary names or resume an interrupted upload.\r\n\r\n"_nx;
    text+="FTP STREAM INSTALL\r\nNSP, NSZ, XCI and XCZ content installs as it arrives, directly to the "_nx+std::string(usb?"USB drive"_nx:"SD card"_nx)+".\r\nNo complete package is copied to SD or USB first. Allow space for the installed content.\r\nYour original package stays on your PC. Existing uploaded packages are left untouched.\r\nWait for Installed successfully on the Switch; FTP confirms success after installation.\r\nIf interrupted, resend the original file from the beginning. Do not use Resume.\r\n\r\nUSB CABLE / MTP\r\nCable uploads are saved in switch/nexusmanager/transfers/install, then installed automatically.\r\nAllow space for the package and installed content. The temporary package is removed after a complete successful install.\r\nFailed or skipped installs keep the package for retry. Your PC original is never removed.\r\n\r\nThis instruction file is read-only and does not get installed.\r\n"_nx;
    return text;
}
// Foreground FTP display aliases only; underlying SD/game devoptabs stay fixed.
inline std::string FtpNativePath(std::string_view path){
    while(path.starts_with('/'))path.remove_prefix(1);
    for(const auto& mapping:{std::pair<std::string_view,std::string_view>{VolumeName(Use::FilesSd),"sdmc"},{FtpGamesName,"games"}}){
        const auto prefix=std::string(mapping.first)+":";
        if(path.starts_with(prefix)&&(path.size()==prefix.size()||path[prefix.size()]=='/'))return std::string(mapping.second)+std::string(path.substr(mapping.first.size()));
    }
    return std::string(path);
}
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
    const bool guide=Installing(use)&&!mutation&&!directory&&relative==std::string("/")+InstallGuideName;
    if(Installing(use) && !relative.empty() && !guide && (directory || relative.find('/',1)!=relative.npos || !nexus::package::Supported(relative)))return false;
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
// Only a single validated installer package in our private staging directory.
inline bool TemporaryInstallPath(std::string_view path){
    const std::string prefix=std::string(Base(Use::InstallSd))+"/";
    if(!path.starts_with(prefix))return false;
    std::string checked;
    return ResolveFor(Use::InstallSd,path.substr(prefix.size()-1),checked,true)&&checked==path;
}
inline bool InstalledEntirePackage(std::uint32_t result,std::size_t installed,std::size_t skipped){
    return !result&&installed>0&&skipped==0;
}
// Raw SD PC-transfer access must not replace an owned upload (including through a FAT
// alias or by renaming its parent) while an install/cleanup is in progress.
inline bool SdMutationAllowed(std::string_view path){
    std::string p(path);for(char& c:p)if(c>='A'&&c<='Z')c+=32;
    if(!p.starts_with("sdmc:/"))return false;
    p.erase(0,5);while(p.starts_with("//"))p.erase(0,1);
    while(p.size()>1&&p.back()=='/')p.pop_back();
    for(size_t pos=1;pos<p.size();){
        auto end=p.find('/',pos);if(end==p.npos)end=p.size();const auto part=std::string_view(p).substr(pos,end-pos);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' '||part.find('~')!=part.npos)return false;
        for(unsigned char c:part)if(c<32||c==127||c==':'||c=='\\'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return false;
        pos=end+1;
    }
    const std::string cache=Base(Use::InstallSd);
    return p!="/"&&p!=cache&&!p.starts_with(cache+"/")&&!cache.starts_with(p+"/");
}
inline bool CartridgeGeometry(std::uint64_t normal,std::uint64_t secure,std::uint32_t last,std::uint64_t& trimmed){
    constexpr std::uint64_t Limit=64ULL*1024*1024*1024;
    trimmed=(std::uint64_t(last)+1)*512;
    return normal>=0x8000 && secure>0 && normal%512==0 && secure%512==0 && normal<=Limit && secure<=Limit-normal && trimmed>=normal && trimmed<=normal+secure;
}
}
