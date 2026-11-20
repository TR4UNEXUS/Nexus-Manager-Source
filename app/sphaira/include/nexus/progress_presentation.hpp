// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace nexus::progressui {
// Keep filenames, NCA identifiers and technical stages in the diagnostic view.
inline std::string Status(std::string stage,std::string action){
    for(auto* text:{&stage,&action})for(auto& c:*text)if(c>='A'&&c<='Z')c+=32;
    const auto has=[&](std::string_view value){return stage.find(value)!=stage.npos;};
    if(has("remov")||has("delet"))return "Removing files";
    if(has("flush")||has("saving")||has("saved"))return "Saving files";
    if(has("verif")||has("check")||has("hash"))return "Checking files";
    if(has("register")||has("application record")||has("ncm database"))return "Adding game to HOME";
    if(has("opening"))return "Opening package";
    if(has("scann")||has("search"))return "Finding files";
    if(has("cop")||has("past"))return "Copying files";
    if(has("extract")||has("unpack"))return "Unpacking files";
    if(has("download"))return "Downloading files";
    if(has("creat"))return "Preparing files";
    if(has("finish")||has("complete"))return "Finishing up";
    if(has("install")||has("writing")||has(".nca")||has(".ncz"))return "Installing game files";
    if(action.find("install")!=action.npos)return "Installing game files";
    if(action.find("cop")!=action.npos||action.find("mov")!=action.npos||action.find("past")!=action.npos)return "Copying files";
    return "Preparing files";
}
// For callbacks reporting cumulative bytes. Never feed weighted percentages or
// item counts into this cursor. Repeated callbacks do not inflate the rate.
struct ByteCursor {
    unsigned long long offset{},total{};
    void Reset(){offset=total=0;}
    unsigned long long Advance(unsigned long long done,unsigned long long size){
        if(!size||done>size)return 0;
        if(size!=total||done<offset){offset=0;total=size;}
        const auto delta=done-offset;offset=done;return delta;
    }
};
}
