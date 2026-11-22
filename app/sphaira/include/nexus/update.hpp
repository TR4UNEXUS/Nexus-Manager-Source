// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/release.hpp"
#include <switch.h>
#include <string>
#include <vector>
#include <atomic>
namespace sphaira::ui {struct ProgressBox;}
namespace nexus::updates {
// Call on the main thread. Startup runs once per process and never opens UI.
void StartStartupCheck(Artifact kind);
// Delivers a verified newer release at most once, without waiting for network I/O.
bool TakeStartupRelease(Release& release);
// Cancel and join before another network session or application teardown.
// A result published before cancellation remains available to TakeStartupRelease.
void StopStartupCheck();
Result Check(sphaira::ui::ProgressBox*,Release&,std::string& detail);
Result Install(sphaira::ui::ProgressBox*,const Release&,std::string& detail);
Result FetchRepairCatalog(sphaira::ui::ProgressBox*,std::vector<u8>&,std::string& detail);
Result SendDiagnosticReport(const std::string&,std::string& receipt,const std::atomic_bool& cancel);
}
