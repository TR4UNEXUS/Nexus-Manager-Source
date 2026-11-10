// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace nexus {
// NCM compares these four fields; the two padding bytes are not identity.
// NS and NCM can return different padding for the same record.
template<class Key> constexpr bool SameMetaKey(const Key& a,const Key& b) {
    return a.id==b.id && a.version==b.version && a.type==b.type && a.install_type==b.install_type;
}
// A matching snapshot is necessary, not proof that an unknown install is stale.
// The UI additionally requires explicit confirmation that other installers are stopped.
constexpr bool SamePlaceholderSnapshot(std::uint64_t before_generation,std::uint64_t now_generation,
    std::int64_t before_size,std::int64_t now_size,bool exists) {
    return exists && before_size>=0 && before_size==now_size && before_generation==now_generation;
}
}
