// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/dump.hpp"
namespace nexus::transfer {
Result PrepareExports(sphaira::ui::ProgressBox*,Target);
void ResetExports();
bool ExportsReady(Target);
u64 ExportGeneration(Target);
Result ExportList(Target,const std::string&,std::vector<Entry>&);
Result ExportType(Target,const std::string&,Info&);
Result OpenExport(Target,const std::string&,std::unique_ptr<sphaira::nexusapp::VirtualNsp>&);
}
