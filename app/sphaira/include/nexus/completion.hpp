// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <array>
#include <string>
#include <vector>
namespace sphaira::nexusapp {
// Advisory checkpoints only: never permission to trust a file or replay a write.
enum class InstallStep : u32 { Started=1, ContentVerified, MetadataCommitted, RecordPublished, Complete, Failed };
struct InstallReceipt {
    std::string path;
    bool usb_source{};
    u64 source_size{}, title{};
    InstallStep step{InstallStep::Started};
    u32 verified{}, result{};
};
Result SaveInstallReceipt(const InstallReceipt&);
Result PendingInstalls(std::vector<InstallReceipt>&);
// Foreground cosmetic queue. No resident thread, PSC participant or boot loop.
Result QueueHomeRefresh(u64 id);
Result FinishHomeRefresh(u64 id);
Result PendingHomeRefresh(std::vector<u64>&);
}
