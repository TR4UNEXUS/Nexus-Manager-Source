// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace nexus::network {
// NAS downloads benefit from a receive window larger than one small RPC. Keep
// the SDK's service, session count, TX/UDP buffers and pool efficiency intact.
// With the pinned libnx defaults this raises transfer memory by exactly 3 MiB.
template<class Config>
constexpr Config DownloadSocketConfig(Config defaults){
    defaults.tcp_rx_buf_size=512*1024;
    defaults.tcp_rx_buf_max_size=1024*1024;
    return defaults;
}

// Matches libnx's _bsdGetTransferMemSizeForConfig. This is a diagnostic only:
// libnx owns the actual allocation, page alignment, cleanup and registration.
template<class Config>
constexpr std::uint64_t SocketTransferMemorySize(const Config& config){
    const std::uint64_t tx=config.tcp_tx_buf_max_size?config.tcp_tx_buf_max_size:config.tcp_tx_buf_size;
    const std::uint64_t rx=config.tcp_rx_buf_max_size?config.tcp_rx_buf_max_size:config.tcp_rx_buf_size;
    const auto sum=tx+rx+config.udp_tx_buf_size+config.udp_rx_buf_size;
    return ((sum+0xfff)&~std::uint64_t{0xfff})*config.sb_efficiency;
}
}
