// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_import_scan.hpp"
#include <memory>
namespace sphaira::nexusapp {
struct UsbImportResult {
    std::vector<std::string> entries;
    std::vector<uint64_t> entry_applications;
    size_t imported{},skipped{};
    Result result{};
    bool restart{},pending{},rolled_back{};
    std::string detail,report_path;
};
Result UsbImportPending(bool& pending);
Result ImportUsbLibrary(ui::ProgressBox*,const UsbImportScan&,UsbImportResult&,bool quick_pc_import=false);
Result RestoreUsbImport(ui::ProgressBox*,UsbImportResult&);
void DisplayUsbImport();
void DisplayPcUsbImport(u64 only_application=0);
bool DispatchPcRemovals();
void AddPcGames(std::shared_ptr<UsbImportScan> scan);
}
