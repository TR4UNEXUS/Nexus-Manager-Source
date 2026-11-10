// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace sphaira::nexusapp {
struct CopyReport {
    const char* stage{"Preparing the copy"};
    const char* event{"move-open"};
    std::string content;
    std::uint64_t offset{}, size{}, verified{};
    // Aggregated in RAM, logged once when the operation returns (not per chunk).
    std::uint64_t read_us{}, write_us{}, hash_us{}, verify_us{}, reuse_verify_us{};
    std::uint64_t copied_bytes{}, saved_source_bytes{}, final_reread_saved_bytes{};
    bool published{};
    void At(const char* name, const char* tag) { stage=name;event=tag; }
    std::string Message() const {
        return std::string(published ? "Files copied and verified. The HOME refresh did not finish." : "Copy stopped.")+
            "\nStopped during: "+stage+".\n\nThe original was not removed."+
            (published ? "\nThe verified destination was kept." :
                "\nThe destination may be incomplete. Keep the original; do not treat this as a completed game.")+
            "\n\nDetails saved to switch/nexusmanager/nexus.log.";
    }
};
struct RemovalReport {
    std::string stage{"Preparing source cleanup"};
    std::size_t removed{}, shared{};
    bool removal_started{};
};
enum class TransferPhase { Copy, Remove, Complete };
struct TransferReport {
    CopyReport copy;
    RemovalReport removal;
    TransferPhase phase{TransferPhase::Copy};
    bool move{};
    std::string Message() const {
        if(phase==TransferPhase::Complete){
            if(!move)return "Copy complete. Both copies were kept.\n\nYour save data is unchanged.";
            return std::string("Move complete. The game is now on the destination.")+ 
                (removal.shared?"\nFiles also used by other games were kept on the source.":"\nThe original game files were removed.")+ 
                "\n\nYour save data is unchanged.";
        }
        if(phase==TransferPhase::Copy)return copy.Message();
        return std::string("The destination copy was verified and kept.\nStopped during: ")+removal.stage+".\n\n"+
            (removal.removal_started?"Source cleanup started. The original may now be incomplete.\nUse the destination copy. Do not delete leftover files manually or start another move to finish cleanup.":
                "The original was not removed. Both copies were kept.")+
            "\n\nYour save data is unchanged. Details saved to switch/nexusmanager/nexus.log.";
    }
};
}
