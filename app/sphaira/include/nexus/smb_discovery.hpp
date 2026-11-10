// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>
#include <vector>
namespace sphaira::ui {struct ProgressBox;}
namespace sphaira::nexusapp {
struct SmbServer {std::string host,address,label;};
// Caller holds socket/nifm initialization for this foreground operation.
// No login, share access, filesystem writes or saved credentials are involved.
Result DiscoverSmbServers(ui::ProgressBox*,std::vector<SmbServer>&,std::string& detail);
}
