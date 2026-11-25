// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "yati/yati.hpp"
#include <string>
#include <vector>
#include <atomic>
namespace sphaira::nexusapp {
enum class UsbImportStatus { Ready, AlreadyRegistered, Incomplete, TicketMissing };
enum class UsbImportRegistration { Unknown, Usb, Native, Superseded };
struct UsbImportTitle {
    yati::ExistingUsbRecord record;
    UsbImportStatus status{UsbImportStatus::Incomplete};
    std::string name,reason,meta_path;
    std::vector<FsRightsId> required_tickets,archived_tickets;
    UsbImportRegistration registration{UsbImportRegistration::Unknown};
    bool missing_tickets{};
    bool needs_home_import{}; // Verified USB item lacks a current-system record or has an available original ticket.
};
struct UsbImportScan {
    u64 generation{};
    std::vector<UsbImportTitle> titles;
    size_t nca_files{},unreadable_ncas{},ignored_meta{};
    // Content whose on-drive metadata could not establish ownership. Keep
    // identities so deletion can consult the installed database without
    // treating an unreadable unrelated file as permission to delete anything.
    std::vector<NcmContentId> unresolved_ownership;
    bool complete{};
    std::string detail;
};
struct UsbImportArtwork {
    std::string name;
    std::vector<u8> icon;
};
using UsbImportFamily=std::vector<const UsbImportTitle*>;
UsbImportArtwork ReadUsbImportArtwork(u64 generation,const UsbImportFamily&,const std::atomic_bool* cancel=nullptr);
void ShowUsbImportGame(ui::ProgressBox*,u64 generation,const UsbImportFamily&);
struct UsbLibrarySummary {
    size_t affected_games{},repairable_games{},blocked_games{},missing_ticket_games{},selected_items{};
};
struct UsbLibraryPlan {
    UsbLibrarySummary summary;
    std::vector<bool> selected;
    // Empty for unaffected/repairable families; otherwise explains why no
    // member of this family may be changed by this plan.
    std::vector<std::string> family_reason;
};
UsbLibraryPlan PlanUsbLibrary(const UsbImportScan&);
UsbLibrarySummary SummarizeUsbLibrary(const UsbImportScan&);
// No placeholder, ticket, metadata, routing or game-file writes are performed.
// Background callers supply cancellation and names=false: no UI or shared
// title-name cache is accessed. The caller must stop/join before mutations.
Result ScanUsbImports(ui::ProgressBox*,UsbImportScan&,u64 expected_generation=0,
    const std::atomic_bool* cancel=nullptr,bool names=true,bool ownership_only=false);
bool HeaderType(u8 content_type,u8 header_type);
std::string CanonicalUsbContentPath(const NcmContentId&);
const char* UsbImportStatusText(UsbImportStatus);
std::string UsbImportPreview(const UsbImportScan&);
}
