// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include "nexus/routing_backup_core.hpp"
namespace sphaira::ui {class ProgressBox;}
namespace sphaira::nexusapp {
Result QueryConsoleRoutingBackup(nexus::routing_backup::Status&);
Result CheckpointConsoleRoutingBackup();
Result ConfirmConsoleRoutingBackup(u64& sequence);
Result ReadConsoleRoutingBackup(ui::ProgressBox*,nexus::repair::Snapshot&,nexus::routing_backup::Status&);
Result MissingRoutingWithConsoleBackup(bool& available);
void DisplayConsoleRoutingBackup();
void PollAutomaticRoutingBackup();
void StopAutomaticRoutingBackup();
void BackupConsoleRoutingAfterChange();
}
