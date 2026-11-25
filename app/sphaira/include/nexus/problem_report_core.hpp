// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace nexus::reports {
constexpr std::size_t MaxReport=128*1024,MaxQueued=12;
inline bool Kind(std::string_view s){return s=="install"||s=="transfer"||s=="delete"||s=="repair"||s=="import"||s=="manual";}
inline bool Token(std::string_view s,std::size_t n){return !s.empty()&&s.size()<=n&&std::all_of(s.begin(),s.end(),[](char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';});}
inline bool Hex(std::string_view s,std::size_t n){return s.size()==n&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
inline bool Number(std::string_view s){if(s.starts_with("0x"))return s.size()>=3&&s.size()<=18&&Hex(s.substr(2),s.size()-2);return !s.empty()&&s.size()<=20&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});}
inline std::string Escape(std::string_view s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){char b[7]{};snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=c;}return out+'"';}
inline bool Description(std::string_view s){
    if(s.size()>2000||s.find("-----BEGIN")!=s.npos||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32&&c!='\n'&&c!='\t';}))return false;
    std::string lower(s);for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;
    for(const auto word:{"password","passwd","authorization","api key","api_key","api-key","secret","token"}){
        auto pos=lower.find(word);while(pos!=lower.npos){auto end=pos+std::char_traits<char>::length(word);while(end<lower.size()&&(lower[end]==' '||lower[end]=='\t'))++end;if(end<lower.size()&&(lower[end]=='='||lower[end]==':'))return false;pos=lower.find(word,pos+1);}
    }return true;
}
inline bool StatusFields(std::string_view text){
    constexpr std::array<std::string_view,8> keys{"usb_query=","mounted=","ready=","state=","usb_result=","backup_query=","backup_flags=","backup_result="};
    for(unsigned i=0;i<keys.size();++i){
        if(!text.starts_with(keys[i]))return false;text.remove_prefix(keys[i].size());
        const auto space=text.find(' ');const auto value=text.substr(0,space);
        if(i==0||i==4||i==5||i==7){if(!value.starts_with("0x")||!Hex(value.substr(2),8))return false;}
        else if(value.empty()||value.size()>(i==6?10u:3u)||!std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';}))return false;
        if(i==keys.size()-1)return space==text.npos;
        if(space==text.npos)return false;text.remove_prefix(space+1);
    }return false;
}
inline std::string ManagerLines(std::string_view input){
    std::istringstream stream{std::string(input)};std::string line,session;
    using Row=std::pair<size_t,std::string>;std::vector<Row> safe,context;size_t sequence{};
    while(std::getline(stream,line)){
        if(line.size()>640||!line.starts_with("NM2 "))continue;
        std::istringstream fields(line);std::array<std::string,10> f;bool valid=true;
        for(auto&field:f)if(!(fields>>field)){valid=false;break;}std::string extra;if(!valid||fields>>extra)continue;
        if(f[0]!="NM2"||f[1].size()!=20||!std::all_of(f[1].begin(),f[1].end(),[](char c){return(c>='0'&&c<='9')||c=='-'||c==':'||c=='T'||c=='Z';}))continue;
        const std::array<std::string_view,8> names{"version=","session=","uptime_ms=","operation=","task=","event=","rc=","value="};
        for(unsigned i=0;i<8;++i)if(!f[i+2].starts_with(names[i]))valid=false;if(!valid)continue;
        auto value=[&](unsigned i){return std::string_view(f[i+2]).substr(names[i].size());};
        if(value(0).size()>15||!std::all_of(value(0).begin(),value(0).end(),[](char c){return(c>='0'&&c<='9')||c=='.';})||
           !Hex(value(1),16)||!Number(value(2))||!Number(value(3))||!Token(value(4),64)||!Token(value(5),192)||
           !value(6).starts_with("0x")||!Hex(value(6).substr(2),8)||!Number(value(7)))continue;
        if(session!=value(1)){context.clear();session=value(1);}
        const auto event=value(5);
        const bool keep=event=="install-content-program-id"||event.starts_with("install-package-title-")||
            event=="install-package-format"||event=="install-source-bytes"||event=="install-local-source"||
            event=="install-network-source"||event=="install-ftp-stream"||event=="install-destination"||
            event=="move-begin-M4"||event=="move-route-M4";
        if(event=="operation-begin")context.clear();
        const Row row{sequence++,line};
        if(keep){context.push_back(row);if(context.size()>16)context.erase(context.begin());}
        safe.push_back(row);if(safe.size()>80)safe.erase(safe.begin());
    }
    for(const auto& row:context)if(std::none_of(safe.begin(),safe.end(),[&](const Row& s){return s.first==row.first;}))safe.push_back(row);
    std::sort(safe.begin(),safe.end(),[](const Row& a,const Row& b){return a.first<b.first;});
    while(safe.size()>80){const auto disposable=std::find_if(safe.begin(),safe.end(),[&](const Row& row){
        return std::none_of(context.begin(),context.end(),[&](const Row& pin){return pin.first==row.first;});});
        if(disposable==safe.end())break;safe.erase(disposable);}
    std::string out;for(const auto& [index,s]:safe)if(out.size()+s.size()+1<=48*1024)out+=s+'\n';return out;
}
inline std::string FirmwareLines(std::string_view input){
    std::istringstream stream{std::string(input)};std::string line,out;
    while(std::getline(stream,line)){
        const auto at=line.find('=');if(at==line.npos)continue;const std::string_view key(line.data(),at),value(line.data()+at+1,line.size()-at-1);
        if(key=="build"||key=="build_id"){if(Token(value,96)&&out.size()+line.size()+1<=24*1024)out+=line+'\n';continue;}
        if(!Number(value))continue;
        // Keep numeric sleep/reconnect evidence inside the existing upload
        // protocol's wake-counter namespace. No free-form paths or text pass.
        if(key.starts_with("usb_sleep_")&&Token(key,70)){
            const auto wake="usb_wake_sleep_"+std::string(key.substr(10))+"="+std::string(value)+"\n";
            if(out.size()+wake.size()<=24*1024)out+=wake;
            continue;
        }
        bool allowed=key=="io_read_us"||key=="io_write_us"||key=="io_sync_us";
        for(const auto prefix:{"usb_transport_","usb_live_","usb_startup_","usb_wake_","usb_last_","usb_first_live_","usb_completion_","loader_first_nonzero_"})allowed|=key.starts_with(prefix);
        if(allowed&&Token(key,80)&&out.size()+line.size()+1<=24*1024)out+=line+'\n';
    }return out;
}
inline std::string Payload(std::string_view manager,std::string_view kind,std::uint32_t result,std::string_view description,std::string_view manager_log,std::string_view firmware_log,std::string_view status){
    if(!Kind(kind)||!Description(description)||!StatusFields(status)||(kind!="manual"&&!description.empty()))return {};
    return "{\"schema\":1,\"manager\":"+Escape(manager)+",\"kind\":"+Escape(kind)+",\"result\":"+std::to_string(result)+
        ",\"description\":"+Escape(description)+",\"logs\":{\"manager\":"+Escape(ManagerLines(manager_log))+",\"firmware\":"+Escape(FirmwareLines(firmware_log))+",\"status\":"+Escape(status)+"}}";
}
inline bool QueueName(std::string_view name){return name.size()==69&&name.ends_with(".json")&&Hex(name.substr(0,64),64);}
}
