// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/manager.hpp"
#include "nexus/index_recovery_core.hpp"
#include "nexus/index_repair_core.hpp"
namespace sphaira::nexusapp {
struct RecoveryContent {
    NcmContentInfo info{}; std::string path;
    bool mismatch{};
    bool converted_hash{};
    bool plaintext_origin{};
    std::array<u8,32> actual_hash{};
};
struct RecoveryMeta {
    NcmContentMetaKey key{}; std::vector<u8> raw;
    u64 application_id{};
    std::string name;
    std::vector<size_t> files;
    bool blocked{};
};
struct RecoveryIssue {
    std::string name, nca, path, expected, actual, kind;
    u64 application_id{}, record_id{};
    u32 version{};
};
struct RecoveryPlan {
    u64 console_backup_sequence{};
    u64 only_application{};
    u64 generation{};
    nexus::recovery::Maps maps;
    nexus::repair::Snapshot before,after;
    nexus::repair::Journal journal;
    std::vector<RecoveryContent> files;
    std::vector<RecoveryMeta> metadata;
    std::vector<RecoveryIssue> issues;
    size_t records{}, complete{}, skipped{}, intact{}, repair_records{}, conflicts{}, limits{};
    size_t mismatch_files{}, blocked_records{}, verified_records{}, repaired_records{};
    size_t converted_files{}, plaintext_files{};
    bool folder_exists{}, ready{}, published{}, pending{};
    bool verification_started{}, verification_complete{}, repair_complete{};
    std::string detail, report_path, report_text;
};
Result ScanUsbRecovery(ui::ProgressBox*, RecoveryPlan&, u64 only_application=0);
Result ApplyUsbRecovery(ui::ProgressBox*, RecoveryPlan&);
Result ScanConsoleRoutingRestore(ui::ProgressBox*, RecoveryPlan&);
Result ApplyConsoleRoutingRestore(ui::ProgressBox*, RecoveryPlan&);
Result ReadUsbRoutingContent(std::set<std::array<u8,16>>&);
Result UsbRoutingReferences(const std::set<std::array<u8,16>>&,bool&);
Result RemoveDeletedUsbRouting(ui::ProgressBox*,u64 generation,const std::vector<NcmContentId>&,
    const std::vector<u64>& applications);
std::string RecoveryIssuePage(const RecoveryPlan&, size_t);
}
