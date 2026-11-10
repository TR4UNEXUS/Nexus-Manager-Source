// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/index_repair_core.hpp"
#include <string>

namespace nexus::health {
struct Report { std::string detail; bool offer_scan{}; };
// IO adapter exposes only read-only operations. A ready report is invalidated
// if the drive changes or cancellation occurs before the final Check().
template<class IO> std::uint32_t Run(IO& io, Report& report) {
    report={"USB provider is not ready. Reconnect the drive and check the matching firmware. No files changed.",false};
    if(auto rc=io.Probe())return rc;
    report.detail="USB is connected, but /registered could not be read as a folder. No files changed.";
    if(auto rc=io.Registered())return rc;
    report.detail="Could not read the SD recovery state. No files changed.";
    bool pending{};if(auto rc=io.Pending(pending))return rc;
    if(pending){
        if(auto rc=io.Check())return rc;
        report={"An earlier library repair needs attention. Do not install or move games yet. Fully restart, then choose Repair USB library to restore its backup.\n\nThis check changed nothing.",true};
        return 0;
    }
    repair::Snapshot snapshot;
    for(size_t i=0;i<snapshot.size();++i){
        report.detail="Could not safely read "+std::string(repair::Names[i])+". Keep the file and capture the error. No files changed.";
        if(auto rc=io.Read(i,snapshot[i]))return rc;
    }
    report.detail="USB disconnected/changed or check cancelled. Run the check again; no files changed.";
    if(auto rc=io.Check())return rc;
    repair::Indexes indexes;
    if(!repair::Decode(snapshot,indexes)){
        report={"USB is connected, but its game library could not be read safely. Nothing was overwritten.\n\nKeep the library files and backups for diagnosis. Automatic repair cannot safely fix this library.",false};
        return 0;
    }
    size_t missing{};for(const auto& image:snapshot)if(!image.exists)++missing;
    bool incomplete=indexes.titles.empty() || indexes.mappings.empty() || indexes.content.empty();
    for(const auto& [id,alias]:indexes.titles){(void)alias;if(!indexes.mappings.contains(id))incomplete=true;}
    for(const auto& [id,ids]:indexes.mappings){
        if(!indexes.titles.contains(id))incomplete=true;
        for(const auto& content:ids)if(repair::Nonzero(content) && !indexes.content.contains(content))incomplete=true;
    }
    report.offer_scan=true;
    if(missing || incomplete)
        report.detail="USB is connected. The game library may have missing entries. Choose Repair library to check for safe repairs.";
    else report.detail="USB is connected and its game library can be read.";
    report.detail+="\n\nGame files have not been fully checked. For a game that will not start, select it in My games and choose Check and repair.\n\nNothing was changed.";
    return 0;
}
}
