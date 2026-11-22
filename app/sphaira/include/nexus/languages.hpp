// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace nexus::languages {
struct Choice { long setting; const char* code; const char* name; };
// Saved indexes 0..15 predate regional support; never reorder their meanings.
inline constexpr std::array Choices{
    Choice{0,"","Automatic (Switch language)"},
    Choice{1,"en","English (UK)"}, Choice{16,"en-US","English (US)"},
    Choice{2,"ja","日本語"}, Choice{3,"fr","Français"}, Choice{17,"fr-CA","Français (Canada)"},
    Choice{4,"de","Deutsch"}, Choice{5,"it","Italiano"},
    Choice{6,"es","Español (España)"}, Choice{18,"es-419","Español (Latinoamérica)"},
    Choice{7,"zh-CN","简体中文"}, Choice{12,"zh-TW","繁體中文"}, Choice{8,"ko","한국어"},
    Choice{9,"nl","Nederlands"}, Choice{10,"pt","Português (Portugal)"}, Choice{19,"pt-BR","Português (Brasil)"},
    Choice{11,"ru","Русский"}, Choice{13,"se","Svenska"}, Choice{15,"uk","Українська"}, Choice{14,"vi","Tiếng Việt"}
};
constexpr bool ValidSetting(long setting) {
    for (const auto& choice : Choices) if (choice.setting == setting) return true;
    return false;
}
// libnx SetLanguage values. Both historical and modern Chinese codes exist.
inline constexpr std::array SystemCodes{
    "ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl",
    "pt", "ru", "zh-TW", "en", "fr-CA", "es-419", "zh-CN", "zh-TW", "pt-BR"
};
constexpr const char* Resolve(long setting, int system_language) {
    if (setting != 0)
        for (const auto& choice : Choices) if (choice.setting == setting) return choice.code;
    return system_language >= 0 && system_language < static_cast<int>(SystemCodes.size())
        ? SystemCodes[system_language] : "en";
}
}
