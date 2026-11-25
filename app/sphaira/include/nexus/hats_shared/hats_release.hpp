#pragma once

#include <yyjson.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::hats {

struct ReleaseEntry {
    std::string tag_name;
    std::string name;
    std::string published_at;
    std::string download_url;
    std::string asset_name;
    std::string body;
    std::string author;
    std::string author_url;
    std::uint64_t release_id{};
    std::uint64_t asset_id{};
    std::string asset_updated_at;
    std::string asset_digest;
    std::uint64_t size{};
    bool prerelease{};
};

using AppVersion = std::array<unsigned, 3>;

inline std::optional<AppVersion> ParseAppVersion(std::string_view text) {
    if (text.starts_with('v') || text.starts_with('V')) text.remove_prefix(1);
    AppVersion version{};
    for (size_t i = 0; i < version.size(); ++i) {
        const auto dot = text.find('.');
        const auto part = text.substr(0, dot);
        if (part.empty() || part.size() > 5 || (part.size() > 1 && part[0] == '0')) return {};
        for (char c : part) {
            if (c < '0' || c > '9') return {};
            version[i] = version[i] * 10 + unsigned(c - '0');
        }
        if (i == 2) {
            if (dot != text.npos) return {};
        } else {
            if (dot == text.npos) return {};
            text.remove_prefix(dot + 1);
        }
    }
    return version;
}

inline std::optional<AppVersion> AppReleaseVersion(const ReleaseEntry& entry) {
    auto name = entry.asset_name;
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!name.ends_with(".zip") && !name.ends_with(".nsp")) return {};
    name.resize(name.size() - 4);
    const auto tag = ParseAppVersion(entry.tag_name);
    constexpr std::string_view prefix = "nexus-hats-installer-";
    if (name.starts_with(prefix)) {
        const auto asset = ParseAppVersion(std::string_view(name).substr(prefix.size()));
        // A descriptive release tag is fine; conflicting version numbers are not.
        if (!asset || (tag && asset != tag)) return {};
        return asset;
    }
    return name == "nexus-hats-installer" ? tag : std::nullopt;
}

inline bool IsNewerAppRelease(const ReleaseEntry& entry, std::string_view running_version) {
    const auto current = ParseAppVersion(running_version);
    const auto available = AppReleaseVersion(entry);
    return !entry.prerelease && current && available && *available > *current;
}

// Keep release announcements in Details. Rows and confirmation dialogs use the
// actual pack identity, without the repeated announcement, date or ZIP suffix.
inline std::string PackDisplayName(const ReleaseEntry& entry) {
    std::string name = entry.asset_name;
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.ends_with(".zip")) { name.resize(name.size() - 4); lower.resize(lower.size() - 4); }
    if (lower.starts_with("nexus-")) { name.erase(0, 6); lower.erase(0, 6); }
    const std::pair<std::string_view, std::string_view> variants[] = {
        {"-standard-no-hoc", "Standard No-HOC"},
        {"-standard-hoc", "Standard HOC"},
        {"-nexus-themed-hoc", "Nexus-themed HOC"},
        {"-no-hoc", "No-HOC"}, {"-hoc", "HOC"},
    };
    std::string variant;
    for (const auto& [suffix, label] : variants) {
        if (lower.ends_with(suffix)) {
            name.resize(name.size() - suffix.size());
            variant = label;
            break;
        }
    }
    std::replace(name.begin(), name.end(), '-', ' ');
    std::replace(name.begin(), name.end(), '_', ' ');
    if (name.empty()) name = entry.tag_name;
    if (name.empty()) name = "HATS pack";
    return variant.empty() ? name : name + " - " + variant;
}

// Tags are identifiers, not versions. Use exactly the assets GitHub returns.
inline void ParsePackRelease(yyjson_val* release, std::vector<ReleaseEntry>& entries, std::string_view extension = ".zip") {
    if (!yyjson_is_obj(release) || yyjson_get_bool(yyjson_obj_get(release, "draft"))) return;

    const auto string_value = [](yyjson_val* obj, const char* key) -> std::string {
        const auto* value = yyjson_get_str(yyjson_obj_get(obj, key));
        return value ? value : "";
    };
    ReleaseEntry base;
    base.release_id = yyjson_get_uint(yyjson_obj_get(release, "id"));
    base.tag_name = string_value(release, "tag_name");
    base.name = string_value(release, "name");
    base.published_at = string_value(release, "published_at");
    base.body = string_value(release, "body");
    base.prerelease = yyjson_get_bool(yyjson_obj_get(release, "prerelease"));
    auto* author = yyjson_obj_get(release, "author");
    base.author = string_value(author, "login");
    base.author_url = string_value(author, "html_url");

    size_t idx, max;
    yyjson_val* asset;
    yyjson_arr_foreach(yyjson_obj_get(release, "assets"), idx, max, asset) {
        auto entry = base;
        entry.asset_id = yyjson_get_uint(yyjson_obj_get(asset, "id"));
        entry.asset_updated_at = string_value(asset, "updated_at");
        entry.asset_digest = string_value(asset, "digest");
        entry.asset_name = string_value(asset, "name");
        entry.download_url = string_value(asset, "browser_download_url");
        auto lower = entry.asset_name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Asset names become cache filenames: never accept paths or partial ZIPs.
        if (lower.size() <= extension.size() || !lower.ends_with(extension) ||
            entry.asset_name.find_first_of("/\\:") != std::string::npos ||
            !entry.download_url.starts_with("https://")) continue;
        const auto state = string_value(asset, "state");
        if (!state.empty() && state != "uploaded") continue;
        entry.size = yyjson_get_uint(yyjson_obj_get(asset, "size"));
        entry.name = (base.name.empty() ? base.tag_name : base.name) + " - " + entry.asset_name;
        entries.push_back(std::move(entry));
    }
}

inline auto ParseAppReleases(yyjson_val* root, bool installed_nsp) -> std::vector<ReleaseEntry> {
    std::vector<ReleaseEntry> entries;
    if (yyjson_is_obj(root) && !yyjson_get_bool(yyjson_obj_get(root, "prerelease")))
        ParsePackRelease(root, entries, installed_nsp ? ".nsp" : ".zip");
    // Accept only this installer's release assets, including when a custom feed
    // contains HATS packs or other applications.
    std::erase_if(entries, [](const auto& entry) {
        auto name = entry.asset_name;
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return !name.starts_with("nexus-hats-installer-") &&
            name != "nexus-hats-installer.zip" && name != "nexus-hats-installer.nsp";
    });
    return entries;
}

inline auto ParsePackReleases(yyjson_val* root) -> std::vector<ReleaseEntry> {
    std::vector<ReleaseEntry> entries;
    if (yyjson_is_arr(root)) {
        size_t idx, max;
        yyjson_val* release;
        yyjson_arr_foreach(root, idx, max, release) {
            ParsePackRelease(release, entries);
        }
    } else {
        ParsePackRelease(root, entries);
    }
    // Accept new pack names automatically; omit known non-pack release assets.
    // The installer also requires atmosphere/package3 in the extracted archive.
    std::erase_if(entries, [](const auto& entry) {
        auto name = entry.asset_name;
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return entry.prerelease || name.starts_with("nexus-hats-installer") ||
            name.starts_with("nexus-usb-formatter") || name.starts_with("nexus-manager") ||
            name.starts_with("source.") || name.find("-source.") != std::string::npos ||
            name.find("-source-") != std::string::npos || name.ends_with(".symbols.zip");
    });
    return entries;
}

// Installation receipt for diagnostics. A missing, stale or unwritable receipt
// must not turn an equal/older version into an app update.
inline auto AppUpdateReceipt(const ReleaseEntry& entry) -> std::string {
    if (!entry.release_id || !entry.asset_id || entry.asset_updated_at.empty()) return {};
    return "nexus-installer-release-v1\n" + std::to_string(entry.release_id) + "\n" +
        std::to_string(entry.asset_id) + "\n" + entry.asset_updated_at + "\n" +
        std::to_string(entry.size) + "\n" + entry.asset_digest + "\n";
}

inline bool IsInstalledAppRelease(const std::vector<ReleaseEntry>& entries, const std::string& receipt) {
    if (receipt.empty()) return false;
    return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
        return AppUpdateReceipt(entry) == receipt;
    });
}

} // namespace sphaira::ui::menu::hats
