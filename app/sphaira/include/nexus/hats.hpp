// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include "nexus/hats_catalog.hpp"
namespace sphaira::ui { struct ProgressBox; }
namespace nexus::hats {
inline constexpr char Api[]="https://api.github.com/repos/TR4UNEXUS/Nexus-USB-HATS/releases?per_page=20";
std::string Installed();
bool SupportsPcLibrary();
Result Check(sphaira::ui::ProgressBox*,Release&,std::string&);
bool TakeStartup(Release&);
Result Prepare(sphaira::ui::ProgressBox*,const Pack&,std::string&);
Result Reboot(std::string&);
void Display();
void Prompt(const Release&);
}
