// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/repair_catalog_core.hpp"
#include <switch.h>
namespace sphaira::ui {class ProgressBox;}
namespace nexus::catalog {
inline constexpr char Url[]="https://raw.githubusercontent.com/TR4UNEXUS/Nexus-USB-HATS/main/repair/catalog.nxrc";
struct Loaded {Catalog catalog;Bytes bytes;bool cached{};bool cache_rejected{};};
Result Load(Loaded&);
Result Update(sphaira::ui::ProgressBox*,std::string& detail);
}
