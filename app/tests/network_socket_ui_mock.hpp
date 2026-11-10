// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Lightweight platform declarations for the extracted UI tests. The separate
// Session test uses actual libnx initialization/defaults and injected IPC faults.
#include "nexus/network_socket_config.hpp"
constexpr unsigned Module_Libnx=345,Module_Kernel=1,LibnxError_AlreadyInitialized=10;
constexpr unsigned LibnxError_OutOfMemory=12,KernelError_OutOfMemory=104;
struct SocketInitConfig {
    unsigned tcp_tx_buf_size{0x8000},tcp_rx_buf_size{0x10000};
    unsigned tcp_tx_buf_max_size{0x40000},tcp_rx_buf_max_size{0x40000};
    unsigned udp_tx_buf_size{0x2400},udp_rx_buf_size{0xa500};
    unsigned sb_efficiency{4},num_bsd_sessions{3},bsd_service_type{1};
};
inline const SocketInitConfig* socketGetDefaultInitConfig(){static const SocketInitConfig defaults;return &defaults;}
namespace nexus {inline void Log(const char*,Result=0,std::uint64_t=0){}}
