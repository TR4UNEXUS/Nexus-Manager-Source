// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace nexus::network {
struct RateSample {
    bool active{},sampled{};
    std::uint64_t received{};
    double bytes_per_second{};
};

// Call under the owner's lock. Count received payload, never file offsets or
// installed/decompressed bytes. Half-second wall-clock samples include stalls
// and settle to zero while the installer verifies or publishes local content.
class RateMeter {
    bool active{},sampled{};
    std::uint64_t total{},last_total{},last_ns{};
    double rate{};
public:
    static constexpr std::uint64_t IntervalNs=500'000'000;
    void Begin(std::uint64_t now){active=true;sampled=false;total=last_total=0;last_ns=now;rate=0;}
    void End(){active=sampled=false;total=last_total=last_ns=0;rate=0;}
    void Add(std::uint64_t bytes){
        if(!active)return;
        const auto room=std::numeric_limits<std::uint64_t>::max()-total;
        total+=bytes>room?room:bytes;
    }
    RateSample Sample(std::uint64_t now){
        if(!active)return {};
        if(now<last_ns){last_ns=now;last_total=total;rate=0;sampled=false;}
        const auto elapsed=now-last_ns;
        if(elapsed>=IntervalNs){
            rate=double(total-last_total)*1'000'000'000.0/double(elapsed);
            last_total=total;last_ns=now;sampled=true;
        }
        return {active,sampled,total,rate};
    }
};
}
