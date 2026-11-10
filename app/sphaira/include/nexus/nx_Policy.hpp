// SPDX-License-Identifier: GPL-3.0-or-later
// NEXUS USB integration, 2026. The USB target is an app choice, not an NCM ID.
#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>

namespace nexus {
enum class Target { Sd, Internal, Usb };
inline constexpr bool ValidTarget(Target t) {
    return t==Target::Sd || t==Target::Internal || t==Target::Usb;
}
inline constexpr bool ValidMove(Target source,Target destination) {
    return ValidTarget(source) && ValidTarget(destination) && source!=destination;
}
inline constexpr bool ShareMetadata(Target a,Target b) {
    return ValidTarget(a) && ValidTarget(b) && (a==Target::Internal)==(b==Target::Internal);
}
enum class Provider { Unknown, Absent, Present };
enum class SdRoute { Unavailable, Stock, Native };
inline constexpr std::size_t TransferSize = 1024 * 1024;
inline constexpr const char *TargetName(Target t) {
    return t == Target::Usb ? "USB" : t == Target::Internal ? "Internal Storage" : "SD";
}
inline constexpr SdRoute SelectSdRoute(Provider p, bool native_ok) {
    return p == Provider::Absent ? SdRoute::Stock :
        p == Provider::Present && native_ok ? SdRoute::Native : SdRoute::Unavailable;
}
inline constexpr bool ValidRange(std::uint64_t total, std::uint64_t offset, std::uint64_t size) {
    return offset <= total && size <= total - offset;
}
inline bool VolumePathValid(std::string_view path) {
    if(path.empty() || path.size() >= 0x301 || path.front() != '/') return false;
    if(path == "/") return true;
    if(path.back() == '/') return false;
    for(std::size_t pos = 1; pos < path.size();) {
        auto end = path.find('/', pos);
        if(end == path.npos) end = path.size();
        auto part = path.substr(pos, end - pos);
        if(part.empty() || part == "." || part == "..") return false;
        for(unsigned char c: part) if(c < 32 || c == 127 || c == ':' || c == '\\') return false;
        pos = end + 1;
    }
    return true;
}
inline bool ContentPath(std::string_view common, bool placeholder, std::string &out) {
    out.clear();
    constexpr std::string_view prefix = "@SdCardContent:";
    if(common.size() >= 0x301 || !common.starts_with(prefix)) return false;
    common.remove_prefix(prefix.size());
    while(common.starts_with("//")) common.remove_prefix(1);
    if(!VolumePathValid(common) || !common.ends_with(".nca") ||
        !common.starts_with(placeholder ? "/placehld/" : "/registered/")) return false;
    out = common;
    return true;
}
inline bool RegisteredFilePath(std::string_view path) {
    if(!VolumePathValid(path) || !path.starts_with("/registered/")) return false;
    const auto leaf=path.substr(path.find_last_of('/')+1);
    if(leaf.size()!=36 || !leaf.ends_with(".nca")) return false;
    for(char c:leaf.substr(0,32))
        if(!((c>='0' && c<='9')||(c>='a' && c<='f')||(c>='A' && c<='F'))) return false;
    return true;
}
}
