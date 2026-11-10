// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace nexus::control {
constexpr bool ValidSize(std::uint64_t actual, std::size_t nacp, std::size_t capacity) {
    return nacp <= capacity && actual >= nacp && actual <= capacity;
}
}
