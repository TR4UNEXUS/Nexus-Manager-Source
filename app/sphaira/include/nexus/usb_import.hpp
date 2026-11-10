// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_import_scan.hpp"
namespace sphaira::nexusapp {
struct UsbImportResult {
    std::vector<std::string> entries;
    size_t imported{},skipped{};
    Result result{};
    bool restart{},pending{},rolled_back{};
    std::string detail,report_path;
};
Result UsbImportPending(bool& pending);
Result ImportUsbLibrary(ui::ProgressBox*,const UsbImportScan&,UsbImportResult&);
Result RestoreUsbImport(ui::ProgressBox*,UsbImportResult&);
void DisplayUsbImport();
}
