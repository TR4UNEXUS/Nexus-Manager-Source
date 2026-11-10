/*
* Notes:
* - nca's that use title key encryption are decrypted using Tegra SE, whereas
*   standard crypto uses software decryption.
*   The latter is almost always (slightly) faster, and removed the need for es patch.
*/

#pragma once

#include "fs.hpp"
#include "source/base.hpp"
#include "container/base.hpp"
#include "ui/progress_box.hpp"
#include <memory>
#include <optional>
#include <array>
#include <string_view>
#include "nexus/nx_Policy.hpp"
#include "nexus/install_diagnostics.hpp"
#include "nexus/completion.hpp"

namespace sphaira::yati {

struct Config {
    bool sd_card_install{};

    // enables downgrading patch / data patch (dlc) version.
    bool allow_downgrade{};

    // ignores the install if already installed.
    // checks that every nca is available.
    bool skip_if_already_installed{};

    // installs tickets only.
    bool ticket_only{};

    // flags to enable / disable install of specific types.
    bool skip_base{};
    bool skip_patch{};
    bool skip_addon{};
    bool skip_data_patch{};
    bool skip_ticket{};

    // enables the option to skip sha256 verification.
    bool skip_nca_hash_verify{};

    // enables the option to skip rsa nca fixed key verification.
    bool skip_rsa_header_fixed_key_verify{};

    // enables the option to skip rsa npdm fixed key verification.
    bool skip_rsa_npdm_fixed_key_verify{};

    // if set, it will ignore the distribution bit in the nca header.
    bool ignore_distribution_bit{};

    // converts a personalised ticket to common.
    bool convert_to_common_ticket{};

    // converts titlekey to standard crypto, also known as "ticketless".
    // this will not work with addon (dlc), so, addon tickets will be installed.
    bool convert_to_standard_crypto{};

    // encrypts the keak with master key 0, this allows the game to be launched on every fw.
    // implicitly performs standard crypto.
    bool lower_master_key{};

    // sets the system_firmware field in the cnmt extended header.
    // if mkey is higher than fw version, the game still won't launch
    // as the fw won't have the key to decrypt keak.
    bool lower_system_version{};
};

// Optional per-package result, including an intentional no-op (older update).
// A record counts as installed only after metadata and HOME publication pass.
struct InstallOutcome {size_t installed_records{},skipped_records{};};

// overridable options, set to avoid
struct ConfigOverride {
    // NAS installs may finish with size/header checks after streamed source
    // hashing and acknowledged writes. Other installs/imports keep readback.
    bool quick_usb_final_verify{};
    nexusapp::InstallReceipt* receipt{};
    // Optional caller-owned output; never accessed by worker threads.
    nexus::install::Diagnostic* diagnostic{};
    InstallOutcome* outcome{};
    std::optional<nexus::Target> nexus_target{};
    // Nonzero pins a batch to the originally selected USB destination device.
    u64 nexus_destination_generation{};
    std::optional<bool> sd_card_install{};
    std::optional<bool> ticket_only{};
    std::optional<bool> skip_base{};
    std::optional<bool> skip_patch{};
    std::optional<bool> skip_addon{};
    std::optional<bool> skip_data_patch{};
    std::optional<bool> skip_ticket{};
    std::optional<bool> skip_nca_hash_verify{};
    std::optional<bool> skip_rsa_header_fixed_key_verify{};
    std::optional<bool> skip_rsa_npdm_fixed_key_verify{};
    std::optional<bool> ignore_distribution_bit{};
    std::optional<bool> convert_to_common_ticket{};
    std::optional<bool> convert_to_standard_crypto{};
    std::optional<bool> lower_master_key{};
    std::optional<bool> lower_system_version{};
    // Keep exact source tickets/certificates on USB for this console's other
    // SD/emuMMC environments. A failed archive is reported, never hidden.
    std::optional<bool> archive_usb_tickets{};
};

struct ExistingUsbContent {
    NcmContentInfo info{};
    std::array<u8,32> expected_hash{};
    bool has_expected_hash{};
};
struct ExistingUsbRecord {
    NcmContentMetaKey key{};
    std::vector<u8> installed_meta;
    // Exactly the installed_meta content-info order: Meta NCA first, then
    // original CNMT references. No aliases, missing files or guessed IDs.
    std::vector<ExistingUsbContent> content;
    u64 application_id{};
};
// Read-only physical USB header/size/full-hash verification; no NCM routing,
// placeholder creation, ticket import, registration or filesystem writes.
Result VerifyExistingUsbRecord(ui::ProgressBox*,u64 generation,const ExistingUsbRecord&);
// Coordinator must persist original indexes, raw NCM and NS records before
// calling. Publishes existing bytes without copying/deleting NCAs. Does not
// update AVM or HOME badges: those belong after coordinator commit.
Result PublishExistingUsbRecord(ui::ProgressBox*,u64 generation,const ExistingUsbRecord&,
    u32 latest_version,nexus::install::Diagnostic* diagnostic=nullptr);

Result InstallFromFile(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& path, const ConfigOverride& override = {});
Result InstallFromSource(ui::ProgressBox* pbox, source::Base* source, const fs::FsPath& path, const ConfigOverride& override = {}, s64 source_size = -1);
Result InstallFromContainer(ui::ProgressBox* pbox, container::Base* container, const ConfigOverride& override = {});
Result InstallFromCollections(ui::ProgressBox* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override = {});

// Only the already-installed native Manager title, from a verified local NSP.
// Preserves its NS key/storage and old NCAs; snapshots and rolls back metadata.
Result CheckManagerUpdateStorage(NcmStorageId storage,std::string& detail);
Result InstallManagerUpdateFromLocalNsp(ui::ProgressBox*,fs::Fs*,const fs::FsPath&,
    std::string_view expected_version,std::string_view expected_sha256,u64 expected_size,
    NcmStorageId storage,std::string& detail);

} // namespace sphaira::yati
