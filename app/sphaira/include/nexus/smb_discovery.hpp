// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>
#include <vector>
namespace sphaira::ui {struct ProgressBox;}
namespace sphaira::nexusapp {
struct SmbServer {std::string host,address,label,protocol{"SMB"};};
// Caller holds socket/nifm initialization for this foreground operation.
// No login, NAS file access or saved credentials are involved. Diagnostic counts
// go to the app's existing local log; server addresses/replies are not logged.
Result DiscoverSmbServers(ui::ProgressBox*,std::vector<SmbServer>&,std::string& detail);
Result DiscoverNetworkServers(ui::ProgressBox*,std::vector<SmbServer>&,std::string& detail,const std::string& protocol={});
}
