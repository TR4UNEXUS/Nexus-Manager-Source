// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/network_location_core.hpp"
#include <switch.h>
namespace sphaira::ui {struct ProgressBox;}
namespace sphaira::nexusapp {
// Caller owns socket/nifm initialization. No remote files are modified.
Result ListNetworkShares(ui::ProgressBox*,const nexus::network::Connection&,const std::string& password,
    std::vector<std::string>&,std::string& detail);
}
