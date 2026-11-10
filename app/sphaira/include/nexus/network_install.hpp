// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Policy.hpp"
#include "nexus/install_diagnostics.hpp"
#include <switch.h>
#include <cstddef>
#include <string>
namespace sphaira::ui {struct ProgressBox;}
namespace sphaira::nexusapp {
// Per-package output, reset for every attempt. Counts include only completed
// title records; an attempted failure may also leave unpublished content.
struct NetworkInstallOutcome {size_t installed_records{},skipped_records{};bool attempted{};};
// mount is a live read-only nexusnet session, path is relative to its share.
// The caller must keep the owning network Session alive until this returns.
// SD is always the physical SD destination. USB requires the generation the
// user selected; neither source nor destination ever falls back to SD.
// NSP/NSZ/XCI/XCZ data is read directly into the installer, never staged locally.
// Network installs intentionally do not create local SD/USB resume receipts.
Result InstallNetworkPackage(ui::ProgressBox* progress,const std::string& mount,
    const std::string& path,u64 expected_size,nexus::Target target,
    u64 destination_generation,nexus::install::Diagnostic& diagnostic,
    NetworkInstallOutcome* result=nullptr);
// Retained for older internal callers; the same supported-format checks apply.
Result InstallNetworkNsp(ui::ProgressBox* progress,const std::string& mount,
    const std::string& path,u64 expected_size,nexus::Target target,
    u64 destination_generation,nexus::install::Diagnostic& diagnostic);
}
