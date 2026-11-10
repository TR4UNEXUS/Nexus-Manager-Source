// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
#include "nexus/port_transfer_core.hpp"
namespace sphaira::nexusapp {
Result PreparePortTransfer(ui::ProgressBox*,const Game&,bool move,nexus::port::Plan&,std::string& detail);
Result TransferPort(ui::ProgressBox*,const nexus::port::Plan&,nexus::port::Report&);
std::string PortTransferPreview(const nexus::port::Plan&);
}
