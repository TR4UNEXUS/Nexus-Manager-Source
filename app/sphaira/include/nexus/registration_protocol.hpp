// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nexus::registration {
inline constexpr std::uint32_t Command = 65032;
inline constexpr std::uint32_t CapabilitiesCommand=65033, CapabilityMagic=0x3350584E;
inline constexpr std::uint32_t MaxTitles=4096, MaxContent=262144;
inline constexpr std::uint32_t FeatureUsbRegistration=1u,FeatureExplicitInstallTarget=1u<<1;
struct Capabilities {std::uint32_t magic{},abi{},titles{},content{},features{},reserved{};};
inline bool Compatible(const Capabilities& c) {
    return c.magic==CapabilityMagic && c.abi==1 && c.titles>=MaxTitles && c.content>=MaxContent && (c.features&FeatureUsbRegistration) && !c.reserved;
}
// U1: native writes/metadata cannot become USB installs merely because a drive
// is attached. Presence/readiness is not a substitute for this explicit bit.
inline bool ExplicitInstallTargetSupported(const Capabilities& c){
    return Compatible(c) && (c.features&FeatureExplicitInstallTarget);
}
// Atmosphere libvapours/results/ncm_results.hpp: module 5, description 4.
inline constexpr std::uint32_t ContentAlreadyExists = 5u | (4u << 9);
// Local app result; never substitutes for an IPC/storage error.
inline constexpr std::uint32_t RepairPending = 505u | (900u << 9);
inline constexpr char PendingJournal[] = "/atmosphere/usb_content-repair.pending";

// SF stable-sorts input arguments by increasing alignment, not declaration
// order: (u64 generation, ContentId id, s64 expected_size) becomes this wire.
template<class ContentId> struct EnsureRequest {
    ContentId content_id;
    std::uint64_t generation;
    std::int64_t expected_size;
};
template<class ContentId> constexpr bool ValidLayout() {
    using Request = EnsureRequest<ContentId>;
    return sizeof(ContentId) == 16 && alignof(ContentId) == 1 &&
        std::is_standard_layout_v<Request> && std::is_trivially_copyable_v<Request> &&
        sizeof(Request) == 32 && offsetof(Request, content_id) == 0 &&
        offsetof(Request, generation) == 16 && offsetof(Request, expected_size) == 24;
}
}
