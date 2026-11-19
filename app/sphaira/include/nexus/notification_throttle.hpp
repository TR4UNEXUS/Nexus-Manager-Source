// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace nexus {
class NotificationThrottle {
    std::uint64_t last{};bool started{};
public:
    bool Ready(std::uint64_t now){
        if(started && now>=last && now-last<250)return false;
        started=true;last=now;return true;
    }
};
}
