// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <cstdint>
namespace nexus::updates {
#ifdef APP_VERSION
inline constexpr char Version[]=APP_VERSION;
#else
// Standalone host tools and fixtures compile without the Switch build flags.
inline constexpr char Version[]="0.2.40";
#endif
inline constexpr char Repository[]="https://github.com/TechRepairs4U/Nexus-Manager";
inline constexpr char ReleaseApi[]="https://api.github.com/repos/TechRepairs4U/Nexus-Manager/releases?per_page=20";
enum class Artifact {Nro,Nsp};
inline constexpr const char* AssetName(Artifact kind){return kind==Artifact::Nsp?"NexusManager.nsp":"NexusManager.nro";}
struct Release {std::string version,tag,url,sha256;std::uint64_t size{};bool preview{};Artifact kind{Artifact::Nro};};
// Only exact x.y.z and vX.Y.Z[-preview] tags, not arbitrary update scripts/URLs.
std::uint32_t VersionCode(std::string_view text);
bool SelectRelease(std::string_view json,Release& out,Artifact kind=Artifact::Nro);
bool ValidRelease(const Release& release);
bool ValidNro(std::string_view bytes,std::string_view version);
bool ValidNsp(std::string_view bytes);
}
