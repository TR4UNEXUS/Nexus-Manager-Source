// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Client.hpp"
#include "nexus/batch.hpp"
#include "nexus/install_diagnostics.hpp"
#include "nexus/transfer_report.hpp"
#include "yati/source/base.hpp"
#include "ui/progress_box.hpp"
#include <array>
#include <string>
#include <vector>

namespace sphaira::nexusapp {
using nexus::Target;
struct Item { std::string name, path; u64 size{}; bool directory{}; };
struct Game {
    u64 id{};
    std::string name;
    std::array<u64,3> bytes{};
    bool location_checked{};
    int badge_state{-1}; // unknown=-1, no USB=0, USB program=1, USB forwarder=2
    bool usb_forwarder{};
    bool forwarder_migratable{};
    unsigned forwarder_address_space{3};
    std::string forwarder_usb_path,forwarder_args;
    bool icon_pending{};
    // NRO rows never enter NS/NCM operations; paths are their cache identity.
    std::string homebrew_path,forwarder_path;
    u64 generation{},file_size{},icon_offset{},icon_size{};
    std::string CacheKey() const {return homebrew_path.empty()?std::to_string(id):"usb:"+homebrew_path;}
};
struct Placeholder { Target target{}; NcmPlaceHolderId id{}; s64 size{}; u64 generation{}; };
Result OpenStorage(Target target, NcmContentStorage& cs, u64* generation = nullptr);
Result ReadContent(Target target, NcmContentStorage& cs, const NcmContentId& id,
    u64 generation, u64 offset, void* buffer, size_t size);
Result ListFiles(bool usb, const std::string& path, std::vector<Item>& items, u64& generation);
Result Install(ui::ProgressBox* progress, bool usb_source, u64 source_generation, const std::string& path, Target target,u64 destination_generation=0,nexus::install::Diagnostic* diagnostic=nullptr);
Result InstallBatch(ui::ProgressBox* progress,bool usb_source,u64 source_generation,nexus::batch::Report& report);
Result ScanGames(ui::ProgressBox* progress, std::vector<Game>& games);
Result InspectGame(ui::ProgressBox* progress,u64 application,Game& game);
Result RefreshHomeBadges(ui::ProgressBox* progress);
Result CopyGame(ui::ProgressBox* progress, u64 app_id, Target source,Target destination,CopyReport* report=nullptr);
Result RemoveVerifiedSource(ui::ProgressBox* progress, u64 app_id, Target source,Target destination,RemovalReport& report);
Result TransferGame(ui::ProgressBox* progress, u64 app_id, Target source,Target destination,bool move,TransferReport& report);
Result ScanPlaceholders(ui::ProgressBox* progress, Target target, std::vector<Placeholder>& result);
Result RemovePlaceholder(const Placeholder& item);
std::string HexId(const void* id, size_t size = 16);
std::string Locations(const Game& game);

// Scratch storage is unregistered, uniquely owned and destroyed after the parser.
class MetadataStage {
    NcmContentStorage cs{};
    NcmPlaceHolderId id{};
    bool owned{};
public:
    ~MetadataStage();
    Result Open(const std::string& usb_common_path, fs::FsPath& native_path);
};
}
