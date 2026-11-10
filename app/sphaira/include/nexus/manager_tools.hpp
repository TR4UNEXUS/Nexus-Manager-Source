// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
namespace sphaira::nexusmanager {
struct Save { FsSaveDataInfo info{}; std::string name; };
Result ListSaves(ui::ProgressBox*,std::vector<Save>&);
Result BackupSave(ui::ProgressBox*,const Save&,std::string& detail,std::string* completed_path=nullptr);
Result ListSaveBackups(ui::ProgressBox*,const Save&,std::vector<std::string>&);
Result RestoreSave(ui::ProgressBox*,const Save&,const std::string& folder,std::string& detail);
Result InspectFile(ui::ProgressBox*,bool usb,u64 generation,const nexusapp::Item&,bool hash,std::string&);
std::string SaveDetails(const Save&);
std::string Display(std::string text);
}
