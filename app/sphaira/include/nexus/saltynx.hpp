// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace nexus::saltynx {
using TitleId = std::uint64_t;
enum class Exception { None, Plugins, WithRomfs, PluginsAndRomfs, Skip };
enum class Advice { Unknown, MarioLaunch, CoreError, PluginsOnly, VersionIssue };
struct Library {
    bool installed{};
    bool settings_readable{};
    std::map<TitleId, Exception> exceptions;
    std::set<TitleId> load_errors;
    Exception Setting(TitleId id) const;
    Advice Guidance(TitleId id, std::string_view name, std::string_view version) const;
};
enum class Save { Added, AlreadySet, Failed };
std::string Entry(TitleId id, Exception mode);
Library Inspect(const std::string& sd = "sdmc:");
Save AddException(TitleId id, Exception mode, const std::string& sd = "sdmc:");
}

