// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/saltynx.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <memory>
#include <string_view>
#include <sys/stat.h>

namespace nexus::saltynx {
namespace {
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
constexpr char Config[] = "/SaltySD/exceptions.txt";
constexpr size_t MaxConfig = 512 * 1024;
constexpr size_t LogTail = 256 * 1024;

bool Regular(const std::string& path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}
bool Installed(const std::string& sd) {
    const std::string module = sd + "/atmosphere/contents/0000000000534C56/exefs";
    return Regular(module + ".nsp") || (Regular(module + "/main") && Regular(module + "/main.npdm"));
}
TitleId ParseId(std::string_view text) {
    if (text.size() < 16) return 0;
    TitleId id{};
    const auto end = text.data() + 16;
    const auto result = std::from_chars(text.data(), end, id, 16);
    return result.ec == std::errc{} && result.ptr == end ? id : 0;
}
template<class F> void Lines(std::string_view text, F visit) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        visit(text.substr(0, end));
        if (end == text.npos) break;
        text.remove_prefix(end + 1);
    }
}
void ReadEntries(std::string_view data, std::map<TitleId, Exception>& entries) {
    Lines(data, [&](std::string_view line) {
        if (line.empty()) return;
        auto mode = Exception::Plugins;
        if (line.front() == 'X' || line.front() == 'x') mode = Exception::Skip;
        if (line.front() == 'R' || line.front() == 'r') mode = Exception::WithRomfs;
        if (mode != Exception::Plugins) line.remove_prefix(1);
        const auto id = ParseId(line);
        if (!id) return;
        auto& current = entries[id];
        if (mode == Exception::Skip || current == Exception::Skip) current = Exception::Skip;
        else if (current == Exception::None || current == mode) current = mode;
        else current = Exception::PluginsAndRomfs;
    });
}
bool ReadConfig(FILE* f, std::string& data) {
    if (std::fseek(f, 0, SEEK_END)) return false;
    const auto length = std::ftell(f);
    if (length < 0 || static_cast<size_t>(length) > MaxConfig || std::fseek(f, 0, SEEK_SET)) return false;
    data.resize(length);
    return std::fread(data.data(), 1, data.size(), f) == data.size() && data.find('\0') == data.npos;
}
void ReadLog(const std::string& path, Library& library) {
    File f(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!f || std::fseek(f.get(), 0, SEEK_END)) return;
    const auto size = std::ftell(f.get());
    if (size <= 0) return;
    const auto length = std::min<size_t>(size, LogTail);
    if (std::fseek(f.get(), -static_cast<long>(length), SEEK_END)) return;
    std::string data(length, '\0');
    data.resize(std::fread(data.data(), 1, length, f.get()));
    bool waiting_for_id = false, loading_core = false;
    TitleId id{};
    Lines(data, [&](std::string_view line) {
        if (line.find("got SD card.") != line.npos || line.find("aborting bootstrap") != line.npos ||
            line.find("not allowing debugging") != line.npos) {
            id = 0;
            waiting_for_id = false;
            loading_core = false;
        }
        if (line.find("found valid CreateProcess event:") != line.npos) {
            id = 0;
            waiting_for_id = true;
            loading_core = false;
        } else if (waiting_for_id) {
            waiting_for_id = false;
            if (const auto tid = line.find("tid "); tid != line.npos) {
                const auto identity = line.substr(tid+4);
                if (identity.size() > 21 && identity.substr(16, 5) == " pid ") {
                    id = ParseId(identity);
                    library.load_errors.erase(id);
                }
            }
        }
        constexpr std::string_view failure = "[serviceLoadELF] Load_elf arm32: ";
        if (line.find("[serviceLoadELF] loading ") != line.npos)
            loading_core = line.find("sdmc:/SaltySD/saltynx_core32.elf,") != line.npos ||
                           line.find("sdmc:/SaltySD/saltynx_core.elf,") != line.npos;
        if (id && loading_core) {
            const auto at = line.find(failure);
            if (at == line.npos) return;
            loading_core = false;
            const auto fields = line.substr(at + failure.size());
            constexpr std::string_view separator = ", ret: 0x";
            if (fields.size() <= 1 + separator.size() || (fields[0] != '0' && fields[0] != '1') ||
                fields.substr(1,separator.size()) != separator) return;
            const auto code = fields.substr(1 + separator.size());
            unsigned result{};
            const auto parsed = std::from_chars(code.data(), code.data()+code.size(), result, 16);
            if (parsed.ec == std::errc{} && result) library.load_errors.insert(id);
        }
    });
}
}

std::string Entry(TitleId id, Exception mode) {
    char out[18]{};
    std::snprintf(out, sizeof(out), "%s%016llX", mode == Exception::Plugins ? "" : "X", static_cast<unsigned long long>(id));
    return out;
}
Exception Library::Setting(TitleId id) const {
    const auto it = exceptions.find(id);
    return it == exceptions.end() ? Exception::None : it->second;
}
Advice Library::Guidance(TitleId id, std::string_view name, std::string_view version) const {
    if (id == 0x0100EA80032EA000ULL) return Advice::MarioLaunch;
    if (load_errors.count(id)) return Advice::CoreError;
    // Upstream's Exceptions table distinguishes core issues from external plugins.
    // Match exact names for editions without a verified regional ID.
    if ((name == "The Witcher 3: Wild Hunt — Complete Edition" ||
         name == "The Witcher 3: Wild Hunt - Complete Edition") && version == "3.2")
        return Advice::VersionIssue;
    if (id == 0x01003A400C3DA000ULL) return Advice::PluginsOnly;
    constexpr std::string_view plugin_only[] = {"Pikmin 3 Deluxe", "Luigi's Mansion 2 HD", "LIMBO",
        "Death Road to Canada", "Cel Damage HD", "Goat Simulator", "Moero Crystal H", "Moero Chronicle Hyper",
        "Monster Hunter Generations Ultimate", "Tokyo Mirage Sessions #FE Encore",
        "Valkyria Chronicles", "World of Goo", "Radiant Silvergun"};
    for (auto title : plugin_only) if (title == name) return Advice::PluginsOnly;
    return Advice::Unknown;
}
Library Inspect(const std::string& sd) {
    Library library;
    library.installed = Installed(sd);
    if (!library.installed) return library;
    File f(std::fopen((sd+Config).c_str(), "rb"), std::fclose);
    if (f) {
        std::string data;
        library.settings_readable = ReadConfig(f.get(), data);
        if (library.settings_readable) ReadEntries(data, library.exceptions);
    } else {
        library.settings_readable = errno == ENOENT;
    }
    ReadLog(sd+"/SaltySD/saltynx.log", library);
    return library;
}
Save AddException(TitleId id, Exception mode, const std::string& sd) {
    if (!id || (mode != Exception::Skip && mode != Exception::Plugins) || !Installed(sd)) return Save::Failed;
    if (mkdir((sd+"/SaltySD").c_str(), 0777) && errno != EEXIST) return Save::Failed;
    File f(std::fopen((sd+Config).c_str(), "a+b"), std::fclose);
    std::string data;
    if (!f || !ReadConfig(f.get(), data)) return Save::Failed;
    std::map<TitleId,Exception> entries;
    ReadEntries(data, entries);
    const auto current = entries[id];
    if (current == Exception::Skip || current == mode ||
        (mode == Exception::Plugins && current == Exception::PluginsAndRomfs)) return Save::AlreadySet;
    const std::string newline = data.find("\r\n") == data.npos ? "\n" : "\r\n";
    const auto suffix = (data.empty() || data.back() == '\n' ? std::string{} : newline) + Entry(id, mode) + newline;
    if (data.size()+suffix.size()>MaxConfig || std::fseek(f.get(), 0, SEEK_END) ||
        std::fwrite(suffix.data(), 1, suffix.size(), f.get()) != suffix.size() ||
        std::fflush(f.get()) || std::fclose(f.release())) return Save::Failed;
    File verify(std::fopen((sd+Config).c_str(), "rb"), std::fclose);
    std::string saved;
    return verify && ReadConfig(verify.get(), saved) && saved == data+suffix ? Save::Added : Save::Failed;
}
}

