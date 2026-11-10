// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_import_scan.hpp"
#include <array>

namespace sphaira::nexusapp {
enum class UsbTicketPrepareStatus { Ready, AlreadyPrepared, NeedsBackup, Incomplete };
struct UsbTicketPrepareTitle {
    std::string name,reason,identity;
    UsbTicketPrepareStatus status{UsbTicketPrepareStatus::Incomplete};
    std::vector<size_t> tickets;
};
struct UsbTicketPrepareCandidate {
    FsRightsId rights{};
    std::array<u8,32> digest{};
    UsbTicketPrepareStatus status{UsbTicketPrepareStatus::NeedsBackup};
    std::string reason;
};
struct UsbTicketPreparePlan {
    u64 generation{};
    std::array<u8,32> source_digest{};
    std::vector<UsbTicketPrepareTitle> titles;
    std::vector<UsbTicketPrepareCandidate> tickets;
    size_t tickets_to_save{};
    bool complete{};
    std::string detail;
};
struct UsbTicketPrepareResult {
    std::vector<std::string> entries;
    size_t prepared{},already_prepared{},needs_backup{},skipped{},saved{};
    Result result{};
    std::string detail,report_path;
};
// Reuses the guided workflow's completed read-only scan. No writes or ticket
// imports. Only exact common ticket/certificate exports from ES are supported.
Result PlanUsbTicketPreparation(ui::ProgressBox*,const UsbImportScan&,UsbTicketPreparePlan&);
// Explicitly confirmed, cancellable USB archive writes. Re-scans the pinned
// drive and rechecks export bytes before any write. Never changes registration.
Result PrepareUsbTickets(ui::ProgressBox*,const UsbTicketPreparePlan&,UsbTicketPrepareResult&);
const char* UsbTicketPrepareStatusText(UsbTicketPrepareStatus);
std::string UsbTicketPreparePreview(const UsbTicketPreparePlan&);
}
