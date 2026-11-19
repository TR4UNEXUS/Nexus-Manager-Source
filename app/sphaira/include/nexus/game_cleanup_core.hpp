// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
namespace nexus::cleanup {
using Id=std::array<std::uint8_t,16>;
struct Reference {std::uint64_t application{};std::set<Id> content;};
struct Selection {std::set<Id> remove,shared;};
// References from all other records (including unregistered games on USB)
// protect shared NCAs. Missing files are allowed when removing a broken game.
inline Selection Select(std::uint64_t application,const std::vector<Reference>& records,
                        const std::set<Id>& physical){
    Selection out;std::set<Id> protected_ids;
    for(const auto& r:records)for(const auto& id:r.content){
        if(r.application==application) {if(physical.contains(id))out.remove.insert(id);}
        else protected_ids.insert(id);
    }
    for(const auto& id:protected_ids)if(out.remove.erase(id))out.shared.insert(id);
    return out;
}
inline std::set<Id> Unreferenced(const std::vector<Reference>& records,const std::set<Id>& physical){
    auto out=physical;for(const auto& r:records)for(const auto& id:r.content)out.erase(id);return out;
}
// A valid user application or homebrew title, never a system title.
inline bool UserApplication(std::uint64_t id){return ((id>>56)==1 && id>=0x0100000000010000ULL) || (id>>56)==5;}
}
