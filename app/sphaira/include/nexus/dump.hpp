// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Client.hpp"
#include "ui/progress_box.hpp"
#include <string>
#include <vector>
#include <memory>

namespace sphaira::nexusapp {
struct DumpRecord {
    NcmContentMetaKey key{};
    std::vector<NcmContentInfo> contents;
};
struct DumpReport {
    std::string stage{"Preparing the backup"}, path, content, detail;
    u64 offset{}, size{};
    nexus::Target destination{nexus::Target::Sd};
    bool completed{}, partial_kept{}, renamed{};
    std::string Message() const;
};
// Inspect each selected physical source, never a routed duplicate on another drive.
Result CollectDumpRecords(u64 app, nexus::Target source, u64 generation, std::vector<DumpRecord>& records);
Result DumpGameToNsp(ui::ProgressBox*, u64 app, const std::string& name, nexus::Target source, nexus::Target destination, DumpReport&);
// Read-only virtual NSP. Only headers/licenses are held in memory; content
// reads are served from the pinned installed source with no temporary dump.
class VirtualNsp {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    VirtualNsp();~VirtualNsp();
    Result Open(u64 app,nexus::Target source,u64 required_generation);
    u64 Size()const;
    Result Read(u64 offset,void* data,u64 size,u64& actual);
};
}
