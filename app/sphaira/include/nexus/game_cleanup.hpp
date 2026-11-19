// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
#include <memory>
namespace sphaira::nexusapp {
enum class CleanupKind { Game, Content, Placeholder, Ticket };
struct CleanupItem {
    CleanupKind kind{};
    Target target{};
    u64 application{},generation{},bytes{};
    NcmContentId content{};
    std::string name,reason;
};
struct CleanupScan {
    std::vector<CleanupItem> items;
    std::string detail;
};
struct RemovalState;
struct TicketRemovalState;
struct GameRemovalPlan {
    std::shared_ptr<const RemovalState> state;
    std::shared_ptr<const TicketRemovalState> ticket_state;
    CleanupItem item;
    size_t files{},shared{},records{};
    u64 bytes{};
    std::string detail;
};
struct GameRemovalReport {
    size_t removed{};
    u64 bytes{};
    bool started{},restart{},complete{};
    std::string detail;
};
struct CleanupBatchEntry {
    CleanupItem item;
    std::array<u8,32> selection{};
    size_t files{};u64 bytes{};
    bool ready{};Result result{};
    std::string detail;
};
struct CleanupBatchPlan {
    std::vector<CleanupBatchEntry> entries;
    size_t ready{},skipped{},files{};u64 bytes{};
    bool complete{},tickets{};
};
struct CleanupBatchReport {
    size_t removed{},skipped{},failed{},files{};u64 bytes{};
    bool restart{},complete{};
    std::string detail;
};
Result PreviewCleanupBatch(ui::ProgressBox*,const std::vector<CleanupItem>&,CleanupBatchPlan&);
Result ApplyCleanupBatch(ui::ProgressBox*,const CleanupBatchPlan&,CleanupBatchReport&);
// Inventory and preview never change games. Only ApplyGameRemoval deletes the
// exact reviewed physical files after a new inventory has matched the preview.
Result ScanCleanup(ui::ProgressBox*,Target,CleanupScan&);
Result ScanUnusedTickets(ui::ProgressBox*,CleanupScan&);
Result ScanGameLocations(ui::ProgressBox*,std::vector<Game>&);
Result PreviewGameRemoval(ui::ProgressBox*,const CleanupItem&,GameRemovalPlan&);
Result ApplyGameRemoval(ui::ProgressBox*,const GameRemovalPlan&,GameRemovalReport&);
}
