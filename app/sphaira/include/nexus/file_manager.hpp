// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
#include "nexus/file_manager_core.hpp"
namespace sphaira::nexusfiles {
struct Selection {bool usb{};u64 generation{};nexusapp::Item item;};
struct Plan {
    Selection source;bool destination_usb{},move{},prepared{};u64 destination_generation{};
    std::string destination_directory,final_path;nexus::files::Snapshot snapshot;
};
struct DeletePlan {Selection source;bool prepared{};nexus::files::Snapshot snapshot;};
using Report=nexus::files::Report;
Result ListFiles(ui::ProgressBox*,bool usb,const std::string& path,std::vector<nexusapp::Item>&,u64& generation);
Result PrepareTransfer(ui::ProgressBox*,const Selection&,bool destination_usb,u64 destination_generation,
    const std::string& destination_directory,bool move,Plan&,std::string& detail);
Result Transfer(ui::ProgressBox*,const Plan&,Report&);
Result PrepareDelete(ui::ProgressBox*,const Selection&,DeletePlan&,std::string& detail);
Result Delete(ui::ProgressBox*,const DeletePlan&,Report&);
std::string TransferPreview(const Plan&);
std::string DeletePreview(const DeletePlan&);
}
