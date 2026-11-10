// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/mods_core.hpp"
#include <switch.h>
namespace sphaira::ui {struct ProgressBox;}
namespace nexus::mods {
Result Find(sphaira::ui::ProgressBox*,const std::string&,std::vector<Candidate>&,u64&,std::string&);
Result Review(sphaira::ui::ProgressBox*,const Candidate&,bool,u64,Plan&,std::string&);
Result Install(sphaira::ui::ProgressBox*,Plan&,u64,std::string&);
Result Restore(sphaira::ui::ProgressBox*,std::string&);
}
