// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <vector>
namespace sphaira::nexusapp {
struct UsbImportScan;
struct PcRemoval;
std::shared_ptr<std::vector<PcRemoval>> TakeDetectedPcRemovals();
void ResumePcInstallDetection();
// Call from the foreground menu. Discovery is read-only and delivers once.
void StartPcInstallDetection();
std::shared_ptr<UsbImportScan> TakeDetectedPcInstalls();
void CancelUsbLibraryStartupCheck();
void StopUsbLibraryStartupCheck();
}
