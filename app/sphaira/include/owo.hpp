#pragma once

#include <switch.h>
#include <string>
#include <vector>
#include "ui/progress_box.hpp"
#include "utils/core.hpp"

namespace sphaira {

enum class ForwarderAddressSpace : u8 {
    Bit36 = 1,
    Bit39 = 3,
};

enum class ForwarderSvcDebugMode : u8 {
    Automatic,
    Enabled,
    Disabled,
};

struct OwoConfig {
    std::string nro_path;
    std::string args{};
    std::string name{};
    std::string author{};
    NacpStruct nacp;
    std::vector<u8> icon;
    std::vector<u8> logo;
    std::vector<u8> gif;
    bool profile_selection{};
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit39};
    CpuCoreMode core_mode{CpuCoreMode::Four};
    bool screenshot{true};
    bool video_capture{true};
    ForwarderSvcDebugMode svc_debug_mode{ForwarderSvcDebugMode::Automatic};

    std::vector<u8> program_nca{};
    // USB Loader creates new, path-bound HOME entries only; never replaces a title.
    std::string usb_path{};
    std::vector<u8> usb_nro_hash{};
    // Migration creates a distinct destination shortcut, including for SD.
    // Never replace an existing application or remove its save data.
    bool port_shortcut{};
    // Opt-in, separately identified SD8 startup measurement launcher.
};

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result;
auto install_forwarder(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result;
auto prepare_core_launch(const std::string& nro_path, const std::string& args, CpuCoreMode core_mode) -> Result;
auto core_launch_pending() -> bool;

} // namespace sphaira
