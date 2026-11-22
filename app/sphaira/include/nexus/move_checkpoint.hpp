// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Policy.hpp"
#include <switch.h>
#include <array>
#include <string>
#include <vector>
namespace sphaira::nexusapp {
struct MoveCheckpointRecord {NcmContentMetaKey key{};std::vector<u8> raw;};
struct MoveCheckpointFile {NcmContentInfo info{};std::array<u8,32> hash{};};
struct MoveCheckpoint {
    u64 application{},sequence{};nexus::Target source{},destination{};
    std::vector<MoveCheckpointRecord> records;
    std::vector<MoveCheckpointFile> files;
};
Result PendingMoves(std::vector<MoveCheckpoint>&);
Result SaveMoveCheckpoint(MoveCheckpoint&);
Result CompleteMoveCheckpoint(const MoveCheckpoint&);
// A checkpoint records what was verified, not permission to delete anything.
// Resume rechecks current metadata, HOME records, storage and every file hash.
}
