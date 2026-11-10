// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/progress_box.hpp"
namespace sphaira::nexusmanager {
Result Cartridge(ui::ProgressBox*,bool dump,bool trimmed,std::string& detail);
Result StorageReport(ui::ProgressBox*,std::string& detail);
}
