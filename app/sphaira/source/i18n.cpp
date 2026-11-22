#include "i18n.hpp"
#include "nexus/languages.hpp"
#include "nexus/translation_format.hpp"
#include "nexus/localize.hpp"
#include "fs.hpp"
#include "log.hpp"
#include <yyjson.h>
#include <vector>
#include <unordered_map>

namespace sphaira::i18n {
namespace {

std::vector<u8> g_i18n_data{};
yyjson_doc* json{};
yyjson_val* root{};
yyjson_doc* override_json{};
yyjson_val* override_root{};
std::unordered_map<std::string, std::string> g_literal_cache{};
std::unordered_map<std::string, std::string> g_tr_cache{};
Mutex g_mutex{};
std::string g_language_code{"en"};

static WordOrder g_word_order = WordOrder::PhraseName;

static WordOrder DetectWordOrder(const std::string& lang) {
    // SOV Language.
    if (lang == "ja" || lang == "ko")
        return WordOrder::NamePhrase;
    
    // Default: SVO Language.
    return WordOrder::PhraseName;
}

static std::string get_locked(std::string_view str, std::string_view fallback) {

    const std::string kkey{str.data(), str.length()};

    if (auto it = g_tr_cache.find(kkey); it != g_tr_cache.end()) {
        return it->second;
    }

    // add default entry
    const auto it = g_tr_cache.emplace(kkey, std::string{fallback}).first;

    if (!json || !root) {
        log_write("no json or root\n");
        return std::string{fallback};
    }

    auto read_value = [](yyjson_val* node) {
        std::string value;
        if (const auto text = yyjson_get_str(node)) value.assign(text, yyjson_get_len(node));
        else if (yyjson_is_arr(node)) {
            size_t idx,max; yyjson_val* elem;
            yyjson_arr_foreach(node,idx,max,elem) {
                if (!yyjson_is_str(elem)) return std::string{};
                if(idx)value += '\n';
                value.append(yyjson_get_str(elem), yyjson_get_len(elem));
            }
        }
        return value;
    };
    std::string ret;
    for (auto candidate : {override_root, root}) {
        if (!candidate) continue;
        auto node = yyjson_obj_getn(candidate, str.data(), str.size());
        if (!node && str != fallback) node = yyjson_obj_getn(candidate, fallback.data(), fallback.size());
        auto value = read_value(node);
        if (!value.empty() && nexus::languages::FormatCompatible(fallback, value)) { ret=std::move(value); break; }
    }
    if (ret.empty()) ret = std::string(fallback);

    // update entry in cache
    g_tr_cache.insert_or_assign(it, kkey, ret);
    return ret;
}

static std::string get_internal(std::string_view str, std::string_view fallback) {
    SCOPED_MUTEX(&g_mutex);
    return get_locked(str, fallback);
}

static std::string get_internal(std::string_view str) {
    return get_internal(str, str);
}

} // namespace

bool init(long index) {
    SCOPED_MUTEX(&g_mutex);

    g_tr_cache.clear();
    R_TRY_RESULT(romfsInit(), false);
    ON_SCOPE_EXIT( romfsExit() );

    u64 languageCode{};
    SetLanguage system_language = SetLanguage_ENGB;
    if (R_SUCCEEDED(setGetSystemLanguage(&languageCode)))
        setMakeLanguage(languageCode, &system_language);
    const std::string lang_name = nexus::languages::Resolve(index, static_cast<int>(system_language));
    g_word_order = DetectWordOrder(lang_name);
    g_language_code = lang_name;
    if (json) yyjson_doc_free(json);
    if (override_json) yyjson_doc_free(override_json);
    json = override_json = nullptr;
    root = override_root = nullptr;
    auto parse = [](const std::vector<u8>& data) -> yyjson_doc* {
        auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(),
            YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS);
        if (doc && !yyjson_is_obj(yyjson_doc_get_root(doc))) { yyjson_doc_free(doc); return nullptr; }
        return doc;
    };
    // Always load the bundled catalogue. A partial or invalid SD override must
    // not hide its translations or make the app appear untranslated.
    const fs::FsPath romfs_path = "romfs:/i18n/" + lang_name + ".json";
    if (R_SUCCEEDED(fs::FsStdio().read_entire_file(romfs_path, g_i18n_data))) json = parse(g_i18n_data);
    if (!json) {
        const fs::FsPath fallback = "romfs:/i18n/en.json";
        if (R_SUCCEEDED(fs::FsStdio().read_entire_file(fallback, g_i18n_data))) json = parse(g_i18n_data);
    }
    if (json) root = yyjson_doc_get_root(json);
    const fs::FsPath sdmc_path = "/config/nexusmanager/i18n/" + lang_name + ".json";
    if (R_SUCCEEDED(fs::FsNativeSd().read_entire_file(sdmc_path, g_i18n_data))) {
        override_json = parse(g_i18n_data);
        if (override_json) override_root = yyjson_doc_get_root(override_json);
        else log_write("Ignoring invalid language override: %s\n", sdmc_path.s);
    }
    log_write("Manager language: %s (setting %ld, system %d)\n", lang_name.c_str(), index, static_cast<int>(system_language));
    return root != nullptr;
}

void exit() {
    SCOPED_MUTEX(&g_mutex);

    if (json) {
        yyjson_doc_free(json);
        json = nullptr;
        root = nullptr;
    }
    if (override_json) yyjson_doc_free(override_json);
    override_json = nullptr; override_root = nullptr;
    g_tr_cache.clear();
    g_i18n_data.clear();
}

std::string get(std::string_view str) {
    return get_internal(str);
}

std::string get(std::string_view str, std::string_view fallback) {
    return get_internal(str, fallback);
}

const char* literal(const char* text, std::size_t size) {
    SCOPED_MUTEX(&g_mutex);
    if (!root) return text; // Global labels may be evaluated before app services start.
    const auto key = g_language_code + "\x1f" + std::string(text, size);
    if (auto it = g_literal_cache.find(key); it != g_literal_cache.end()) return it->second.c_str();
    return g_literal_cache.emplace(key, get_locked({text,size}, {text,size})).first->second.c_str();
}

std::string GetLanguageCode() {
    SCOPED_MUTEX(&g_mutex);
    return g_language_code;
}

// Reorders sentence structure based on locale.
WordOrder GetWordOrder() {
    return g_word_order;
}

bool WordOrderLocale() {
    return g_word_order == WordOrder::NamePhrase;
}

std::string Reorder(std::string_view phrase, std::string_view name) {
    std::string p = i18n::get(phrase);
    std::string out;
    out.reserve(phrase.length() + name.length());

    if (g_word_order == WordOrder::NamePhrase) {
        out.append(name);
        out.append(p);
    } else {
        out.append(p);
        out.append(name);
    }
    return out;
}

} // namespace sphaira::i18n

namespace literals {

std::string operator""_i18n(const char* str, size_t len) {
    return sphaira::i18n::get({str, len});
}

} // namespace literals
