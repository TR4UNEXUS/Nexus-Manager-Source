// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
namespace sphaira::nexusapp {
Result ListHomebrew(const std::string& path,std::vector<Item>& items,u64& generation,bool usb=true);
Result PrepareProbe(ui::ProgressBox* progress,u64 generation,Item& item);
Result ScanHomebrew(ui::ProgressBox* progress,std::vector<Game>& games);
Result ReadHomebrew(const Item& item,u64 generation,Game& game);
Result ReadHomebrewIcon(const Game& game,std::vector<u8>& icon);
Result CheckStartupTimingSupport();
Result CreateSdTimingForwarder(ui::ProgressBox* progress,const Item& item);
Result CreateHomebrewForwarder(ui::ProgressBox* progress,const Item& item,u64 generation,bool timing=false);
}
