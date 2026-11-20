// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace nexus::port {
enum class Phase {Preparing,Copying,Verifying,Launcher,CheckingMove,Removing,Complete};
inline std::string ByteSize(std::uint64_t bytes){
    constexpr double MB=1024.0*1024.0,GB=MB*1024.0,TB=GB*1024.0;
    char text[48];
    std::snprintf(text,sizeof(text),"%.1f %s",bytes>=TB?bytes/TB:bytes>=GB?bytes/GB:bytes/MB,
        bytes>=TB?"TB":bytes>=GB?"GB":"MB");
    return text;
}
struct Progress {
    Phase phase{Phase::Preparing}; bool move{};
    std::uint64_t total{},copied{},reused{},done{},extent{};
    std::string path;
    void Start(Phase next,std::uint64_t size=0){phase=next;done=0;extent=size;path.clear();}
    unsigned Percent() const {
        unsigned begin{},end{};
        switch(phase){
            case Phase::Preparing: return 0;
            case Phase::Copying: begin=5;end=60;break;
            case Phase::Verifying: begin=60;end=move?75:95;break;
            case Phase::Launcher: return move?75:95;
            case Phase::CheckingMove: begin=80;end=90;break;
            case Phase::Removing: begin=90;end=99;break;
            case Phase::Complete: return 100;
        }
        return begin+(extent?(end-begin)*std::min(done,extent)/extent:0);
    }
    const char* Stage() const {
        switch(phase){
            case Phase::Preparing:return "Checking folders and existing files";
            case Phase::Copying:return "Copying game files";
            case Phase::Verifying:return "Verifying copied files";
            case Phase::Launcher:return "Creating HOME shortcut";
            case Phase::CheckingMove:return "Checking originals before removal";
            case Phase::Removing:return "Verifying and removing originals";
            case Phase::Complete:return move?"Port move complete":"Port copy complete";
        }
        return "";
    }
    std::string Bytes() const {
        return "Copied "+ByteSize(copied)+" / "+ByteSize(total-reused)+
            (reused?"   |   Already present "+ByteSize(reused):"");
    }
    std::string Note() const {
        if(phase==Phase::Verifying || phase==Phase::CheckingMove || phase==Phase::Removing)
            return std::string("Checked ")+ByteSize(done)+" / "+ByteSize(extent)+"   |   Keep storage connected";
        if(phase==Phase::Complete)return "All destination files verified";
        return "Game size "+ByteSize(total)+"   |   Keep storage connected";
    }
};
}
