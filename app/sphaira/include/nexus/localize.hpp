// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <type_traits>
namespace sphaira::i18n {
// Immutable, locale-specific storage remains valid for the process lifetime.
// Only explicit UI literals use this API; filenames, IDs and protocol data do not.
const char* literal(const char* text, std::size_t size);
}
inline namespace literals {
constexpr const char* operator""_nx(const char* text, [[maybe_unused]] std::size_t size) {
#ifdef __SWITCH__
    if (!std::is_constant_evaluated()) return sphaira::i18n::literal(text, size);
#endif
    return text;
}
}
