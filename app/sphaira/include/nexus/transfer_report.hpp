// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace sphaira::nexusapp {
struct CopyReport {
    const char* stage{"Preparing the copy"_nx};
    const char* event{"move-open"};
    std::string content;
    std::uint64_t offset{}, size{}, verified{};
    // Aggregated in RAM, logged once when the operation returns (not per chunk).
    std::uint64_t read_us{}, write_us{}, hash_us{}, verify_us{}, reuse_verify_us{};
    std::uint64_t copied_bytes{}, saved_source_bytes{}, final_reread_saved_bytes{};
    bool published{};
    void At(const char* name, const char* tag) { stage=name;event=tag; }
    std::string Message() const {
        return std::string(published ? "Files copied and verified. The HOME refresh did not finish."_nx : "Copy stopped."_nx)+
            "\nStopped during: "_nx+stage+".\n\nThe original was not removed."_nx+
            (published ? "\nThe verified destination was kept."_nx :
                "\nThe destination may be incomplete. Keep the original; do not treat this as a completed game."_nx)+
            "\n\nDetails saved to switch/nexusmanager/nexus.log."_nx;
    }
};
struct RemovalReport {
    std::string stage{"Preparing source cleanup"_nx};
    std::size_t removed{}, shared{};
    bool removal_started{};
};
enum class TransferPhase { Copy, Remove, Complete };
struct TransferReport {
    CopyReport copy;
    RemovalReport removal;
    TransferPhase phase{TransferPhase::Copy};
    bool move{},recovery_saved{};
    std::string Message() const {
        if(phase==TransferPhase::Complete){
            if(!move)return "Copy complete. Both copies were kept.\n\nYour save data is unchanged."_nx;
            return std::string("Move complete. The game is now on the destination."_nx)+
                (removal.shared?"\nFiles also used by other games were kept on the source."_nx:"\nThe original game files were removed."_nx)+
                "\n\nYour save data is unchanged."_nx;
        }
        if(phase==TransferPhase::Copy)return copy.Message();
        return std::string("The destination copy was verified and kept.\nStopped during: "_nx)+removal.stage+".\n\n"+
            (removal.removal_started?"Source cleanup started. The original may now be incomplete.\nUse the destination copy. Do not delete leftover files manually or start another move to finish cleanup."_nx:
                "The original was not removed. Both copies were kept."_nx)+
            (recovery_saved?"\n\nUse Help & recovery > Finish interrupted move. Manager will recheck the destination and remove only the remaining originals; it will not copy the game again."_nx:"")+
            "\n\nYour save data is unchanged. Details saved to switch/nexusmanager/nexus.log."_nx;
    }
};
}
