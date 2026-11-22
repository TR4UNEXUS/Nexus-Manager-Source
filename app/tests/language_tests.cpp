// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_runtime_fixture.hpp"
#include "i18n.hpp"
#include "nexus/languages.hpp"
#include "nexus/localize.hpp"
#include "nexus/translation_format.hpp"
#include <cassert>
#include <set>
int main(){
    using namespace sphaira;
    // Independent expected values for every libnx OS code, including aliases.
    const char* expected[]={"ja","en-US","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","en","fr-CA","es-419","zh-CN","zh-TW","pt-BR"};
    for(auto code:expected)fs::files[std::string("romfs:/i18n/")+code+".json"]="{\"Language\":\""+std::string(code)+"\",\"Back\":\"back translated\",\"Lines\":[\"one\",\"two\"]}";
    for(int system=0;system<18;++system){test_system_language=system;assert(i18n::init(0));assert(i18n::GetLanguageCode()==expected[system]);assert(i18n::get("Language")==expected[system]);assert(std::string("Language"_nx)==expected[system]);i18n::exit();}
    // Existing saved indexes remain stable; new variants never renumber them.
    const char* settings[]={"en","ja","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","se","vi","uk","en-US","fr-CA","es-419","pt-BR"};
    for(int i=1;i<=19;++i)assert(std::string(nexus::languages::Resolve(i,17))==settings[i-1]);
    assert(std::string(nexus::languages::Resolve(-1,17))=="pt-BR");
    assert(std::string(nexus::languages::Resolve(999,999))=="en");
    std::set<long> settings_seen;std::set<std::string> codes;
    for(auto choice:nexus::languages::Choices){assert(settings_seen.insert(choice.setting).second);assert(codes.insert(choice.code).second);assert(nexus::languages::ValidSetting(choice.setting));}
    assert(!nexus::languages::ValidSetting(20));
    test_system_available=false;assert(i18n::init(0));assert(i18n::GetLanguageCode()=="en");i18n::exit();test_system_available=true;
    fs::files["/config/nexusmanager/i18n/fr.json"]="{\"Language\":\"Langue personnalisée\"}";
    assert(i18n::init(3));assert(i18n::get("Language")=="Langue personnalisée");assert(i18n::get("Back")=="back translated");assert(i18n::get("Lines")=="one\ntwo");assert(i18n::get("missing")=="missing");i18n::exit();
    for(auto invalid:{"{broken", "[]", "null", "42"}){fs::files["/config/nexusmanager/i18n/fr.json"]=invalid;assert(i18n::init(3));assert(i18n::get("Language")=="fr");i18n::exit();}
    fs::files.erase("/config/nexusmanager/i18n/fr.json");
    fs::files["romfs:/i18n/fr.json"]="{\"File %u: %s\":\"Fichier %u : %s\",\"Back\":\"Retour\"}";
    fs::files["/config/nexusmanager/i18n/fr.json"]="{\"File %u: %s\":\"Invalid %n\",\"Back\":42}";
    assert(i18n::init(3));assert(i18n::get("File %u: %s")=="Fichier %u : %s");assert(i18n::get("Back")=="Retour");i18n::exit();
    assert(!nexus::languages::FormatCompatible("%u %s","%s %u"));
    assert(nexus::languages::FormatCompatible("%016llX: %.2f MiB/s","ID %016llX : %.2f MiB/s"));
    assert(!nexus::languages::FormatCompatible("%zu bytes","%u bytes"));
    assert(i18n::init(2));const auto old="Language"_nx;assert(i18n::WordOrderLocale());i18n::exit();assert(i18n::init(4));assert(std::string(old)=="ja");assert(std::string("Language"_nx)=="de");assert(!i18n::WordOrderLocale());i18n::exit();
    std::puts("Language runtime passed: all OS codes, persisted indexes, automatic fallback, partial/invalid overrides, arrays and stable literal lifetime.");
}
