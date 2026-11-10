// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/nx_Policy.hpp"
#include <array>
#include <cstdint>

namespace sphaira::ui::nexusview {
struct MoveRoute {
    bool available{};
    nexus::Target source{nexus::Target::Sd}, destination{nexus::Target::Usb};
};
// A badge or an uninspected library row is never evidence of a move source.
// Multiple installed origins require the user to choose explicitly.
inline constexpr MoveRoute PrimaryRoute(const std::array<std::uint64_t,3>& bytes,bool checked) {
    if(!checked)return {};
    unsigned count=0,index=0;
    for(unsigned i=0;i<bytes.size();++i)if(bytes[i]){++count;index=i;}
    if(count!=1)return {};
    const auto source=static_cast<nexus::Target>(index);
    return {true,source,source==nexus::Target::Usb?nexus::Target::Sd:nexus::Target::Usb};
}
}
