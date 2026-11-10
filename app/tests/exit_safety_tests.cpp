#include "nexus/control_bounds.hpp"
#include "nexus/lifecycle.hpp"
#include "nexus/trace_ring.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
    using nexus::control::ValidSize;
    constexpr size_t nacp=0x4000, capacity=0x24000;
    static_assert(!ValidSize(0,nacp,capacity));
    static_assert(!ValidSize(nacp-1,nacp,capacity));
    static_assert(ValidSize(nacp,nacp,capacity));
    static_assert(ValidSize(capacity,nacp,capacity));
    static_assert(!ValidSize(capacity+1,nacp,capacity));
    static_assert(!ValidSize(UINT64_MAX,nacp,capacity));
    static_assert(!ValidSize(capacity,nacp,nacp-1));
    for(uint64_t size=0;size<capacity+4096;size++)
        assert(ValidSize(size,nacp,capacity)==(size>=nacp && size<=capacity));
    assert(nexus::lifecycle::CanPoll());
    nexus::lifecycle::SetForeground(false); assert(!nexus::lifecycle::CanPoll());
    nexus::lifecycle::SetForeground(true); assert(nexus::lifecycle::CanPoll());
    nexus::lifecycle::BeginShutdown(); assert(!nexus::lifecycle::CanPoll());
    nexus::lifecycle::SetForeground(true); assert(!nexus::lifecycle::CanPoll());
    nexus::startup::TraceRing ring{0,102};
    for(size_t i=0;i<100000;i++) { assert(ring.Offset()==(i%102)*320); ring.Advance(); }
    std::puts("PASS control-size boundaries, foreground/shutdown polling gate, bounded trace offsets");
}
