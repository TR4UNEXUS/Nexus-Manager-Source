// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <string>
#include <string_view>
namespace nexus::progressui {
// Keep filenames, NCA identifiers and technical stages in the diagnostic view.
inline std::string Status(std::string stage,std::string action){
    for(auto* text:{&stage,&action})for(auto& c:*text)if(c>='A'&&c<='Z')c+=32;
    const auto has=[&](std::string_view value){return stage.find(value)!=stage.npos;};
    if(has("remov")||has("delet"))return "Removing files"_nx;
    if(has("flush")||has("saving")||has("saved"))return "Saving files"_nx;
    if(has("verif")||has("check")||has("hash"))return "Checking files"_nx;
    if(has("register")||has("application record"_nx)||has("ncm database"_nx))return "Adding game to HOME"_nx;
    if(has("opening"))return "Opening package"_nx;
    if(has("scann")||has("search"))return "Finding files"_nx;
    if(has("cop")||has("past"))return "Copying files"_nx;
    if(has("extract")||has("unpack"))return "Unpacking files"_nx;
    if(has("download"))return "Downloading files"_nx;
    if(has("creat"))return "Preparing files"_nx;
    if(has("finish")||has("complete"))return "Finishing up"_nx;
    if(has("install")||has("writing")||has(".nca")||has(".ncz"))return "Installing game files"_nx;
    if(action.find("install")!=action.npos)return "Installing game files";
    if(action.find("cop")!=action.npos||action.find("mov")!=action.npos||action.find("past")!=action.npos)return "Copying files";
    return "Preparing files"_nx;
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
