// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/problem_report_core.hpp"
#include <charconv>
namespace nexus::reports {
struct CrashObservation {std::uint64_t program{},timestamp{};std::uint32_t result{},type{};bool valid{};};
inline bool ParseNumber(std::string_view text,std::uint64_t& number,int base){
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number,base);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size();
}
inline CrashObservation Crash(std::string_view name,std::string_view text){
    CrashObservation result;
    if(name.size()!=32||name[11]!='_'||!name.ends_with(".log")||!Hex(name.substr(12,16),16)||
        !ParseNumber(name.substr(0,11),result.timestamp,10)||!ParseNumber(name.substr(12,16),result.program,16)||!result.program||
        text.size()>512*1024||!text.starts_with("Atmosph"))return {};
    const auto header=text.substr(0,text.find('\n'));const bool fatal=header.find("Fatal Report")!=header.npos;
    if(!fatal&&header.find("Crash Report")==header.npos)return {};
    if(fatal)result.type=4;
    bool has_program{},has_result{};std::istringstream stream{std::string(text)};std::string line;
    while(std::getline(stream,line)){
        std::string_view value(line);auto at=value.find_first_not_of(" \t\r");if(at==value.npos)continue;value.remove_prefix(at);
        const auto field=[&](std::string_view prefix){auto v=value.substr(prefix.size());auto start=v.find_first_not_of(" \t");if(start==v.npos)return std::string_view{};v.remove_prefix(start);return v.substr(0,v.find_first_of(" \t\r"));};
        if(value.starts_with("Program ID:")){std::uint64_t id{};auto id_text=field("Program ID:");if(has_program||!Hex(id_text,16)||!ParseNumber(id_text,id,16)||id!=result.program)return {};has_program=true;}
        if(value.starts_with("Result:")){auto code=field("Result:");std::uint64_t parsed{};if(has_result||!code.starts_with("0x")||!Hex(code.substr(2),code.size()-2)||code.size()>10||!ParseNumber(code.substr(2),parsed,16))return {};result.result=parsed;has_result=true;}
        if(value.starts_with("Type:")){if(value.find("User Break")!=value.npos)result.type=1;else if(value.find("Data Abort")!=value.npos)result.type=2;else if(value.find("Instruction Abort")!=value.npos)result.type=3;}
    }
    result.valid=has_program&&has_result;return result;
}
// Retain the existing receiver envelope. Origin is explicit in structured
// events, so an automatic diagnostic never bypasses a disabled preference.
inline bool IsAutomaticCapture(std::string_view manager){
    const auto start=manager.rfind(" event=report-origin ");if(start==manager.npos)return false;
    const auto line=manager.substr(start,manager.find('\n',start)-start);
    return line.ends_with(" value=1");
}
}
