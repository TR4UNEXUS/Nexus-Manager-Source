// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include "nexus/registration_protocol.hpp"

namespace nexus::presentation {
// Presentation only. Never change the result, skip verification or retry writes.
constexpr const char* ContentReason(std::uint32_t result) {
    if(result==registration::RepairPending)
        return "An earlier USB repair is unfinished. Restart the Switch, then open Options > Check / repair USB games before making more changes.";
    if(result==(10u|(221u<<9)) || result==(10u|(1u<<9)))
        return "This feature needs the matching USB firmware. Install the firmware supplied with this app, fully restart the Switch, then try again.";
    if((result&0x1ffu)!=2)return nullptr;
    switch((result>>9)&0x1fffu){
    case 4518: case 4519:
        return "The system could not accept this game's signed information. Check that the app and USB firmware match. This does not by itself mean the game files are damaged.";
    case 1004: case 6451: case 6452:
        return "The information needed to unlock this game's content is missing or could not be used. Check the game's original installation and matching firmware; repairing the game list cannot replace it.";
    case 4520: case 4549: case 4612: case 4613: case 4643:
        return "The game did not pass a file check. A changed file or a read problem may be responsible. Keep the drive connected and use Options > Check / repair USB games to investigate.";
    case 4543:
        return "This game's format is not supported by the current reader. Check for a matching app and firmware update. Do not delete your saves or format the drive.";
    case 4523: case 4524: case 4525: case 4526: case 4529: case 4547: case 5326:
        return "The system could not read this game's format. Check that the app and USB firmware match. This result alone does not prove the files are damaged.";
    default:return nullptr;
    }
}

inline bool Hex(char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
inline bool NumericDiagnostic(std::string_view value){
    bool digit=false;
    for(char c:value){
        if(c>='0'&&c<='9')digit=true;
        else if(!Hex(c)&&c!='x'&&c!='X'&&c!=' '&&c!='\t'&&c!='-'&&c!='/'&&c!=':')return false;
    }
    return digit;
}
// Keep operation-specific warnings (including partial changes). Hide only raw
// diagnostics; the unmodified message and result are logged by ErrorBox first.
inline std::string Context(std::string_view raw){
    std::string out;
    for(size_t start=0;start<raw.size();){
        const auto end=raw.find('\n',start);
        std::string line(raw.substr(start,end==raw.npos?raw.size()-start:end-start));
        start=end==raw.npos?raw.size():end+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto first=line.find_first_not_of(" \t");
        std::string_view trimmed=first==line.npos?std::string_view{}:std::string_view(line).substr(first);
        bool diagnostic=false;
        for(std::string_view prefix:{"Code ","Code:","Module:","Description:","Result:","rc="})
            if(trimmed.starts_with(prefix)&&NumericDiagnostic(trimmed.substr(prefix.size())))diagnostic=true;
        // Only discard a complete structured code row, never a prose warning.
        const auto description=trimmed.find(" / Description ");
        if(trimmed.starts_with("Code 0x")&&trimmed.find(" / Module ")!=trimmed.npos&&
           description!=trimmed.npos&&NumericDiagnostic(trimmed.substr(description+15)))diagnostic=true;
        if(diagnostic)continue;
        for(size_t p=0;(p=line.find("0x",p))!=line.npos;){
            size_t q=p+2;while(q<line.size()&&Hex(line[q]))++q;
            if(q==p+2){p=q;continue;}
            line.replace(p,q-p,"[details in log]");p+=16;
        }
        for(const char* prefix:{"SphairaError_","USBLoader_","FsError_","SvcError_"}){
            for(size_t p=0;(p=line.find(prefix,p))!=line.npos;){
                size_t q=p;while(q<line.size()&&((line[q]>='a'&&line[q]<='z')||(line[q]>='A'&&line[q]<='Z')||(line[q]>='0'&&line[q]<='9')||line[q]=='_'))++q;
                line.replace(p,q-p,"[details in log]");p+=16;
            }
        }
        if(!out.empty())out+='\n';
        out+=line;
    }
    return out;
}
inline std::string Message(const char* reason,std::string_view context){
    auto clean=Context(context);
    if(reason)return std::string(reason)+(clean.empty()?"":"\n\n"+clean);
    if(!clean.empty() && clean!="[details in log]")return clean;
    return "Nexus Manager couldn't complete this request. The cause isn't known yet. Check the app log before trying again.";
}
}
