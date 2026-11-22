// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
using u8=std::uint8_t; using u64=std::uint64_t; using Result=unsigned; using Mutex=std::mutex;
enum SetLanguage {SetLanguage_JA,SetLanguage_ENUS,SetLanguage_FR,SetLanguage_DE,SetLanguage_IT,SetLanguage_ES,SetLanguage_ZHCN,SetLanguage_KO,SetLanguage_NL,SetLanguage_PT,SetLanguage_RU,SetLanguage_ZHTW,SetLanguage_ENGB,SetLanguage_FRCA,SetLanguage_ES419,SetLanguage_ZHHANS,SetLanguage_ZHHANT,SetLanguage_PTBR};
inline int test_system_language=SetLanguage_ENGB;inline bool test_system_available=true;
inline Result setGetSystemLanguage(u64* code){*code=test_system_language;return test_system_available?0:1;}
inline Result setMakeLanguage(u64 code,SetLanguage* out){*out=static_cast<SetLanguage>(code);return 0;}
inline Result romfsInit(){return 0;}inline void romfsExit(){}
inline void log_write(const char*,...){}
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
#define R_TRY_RESULT(x,result) do{if(R_FAILED(x))return result;}while(0)
#define SCOPED_MUTEX(x) std::lock_guard lock(*(x))
template<class F>struct ScopeExit{F f;~ScopeExit(){f();}};
#define ON_SCOPE_EXIT(x) ScopeExit cleanup{[&](){x;}}
namespace fs {
inline std::unordered_map<std::string,std::string> files;
struct FsPath{char s[1024]{};FsPath(const char* value){std::strncpy(s,value,1023);}FsPath(const std::string& value):FsPath(value.c_str()){};};
struct FsStdio{Result read_entire_file(const FsPath& p,std::vector<u8>& data){auto it=files.find(p.s);if(it==files.end())return 1;data.assign(it->second.begin(),it->second.end());return 0;}};
using FsNativeSd=FsStdio;
}
