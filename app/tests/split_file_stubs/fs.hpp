#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <algorithm>
#define FS_MAX_PATH 769
constexpr unsigned FsOpenMode_Read=1,FsDirOpenMode_ReadFiles=2;
struct FsDirectoryEntry {char name[FS_MAX_PATH]{};};
namespace fake {
inline std::map<std::string,std::vector<u8>> files;
inline std::string fail_open, fail_read;
inline bool short_read{},zero_read{},fail_dir{};
inline unsigned opens{},active{},max_active{};
}
namespace fs {
struct FsPath {char s[FS_MAX_PATH]{};FsPath()=default;FsPath(const std::string& a){std::strcpy(s,a.c_str());}FsPath(const char* a){std::strcpy(s,a);}};
struct File {
 std::string path;
 ~File(){Close();}
 void Close(){if(!path.empty()){--fake::active;path.clear();}}
 Result GetSize(s64* out){if(path.empty())return 91;*out=fake::files.at(path).size();return 0;}
 Result Read(s64 off,void* out,u64 n,u32,u64* got){
  *got=0;if(path.empty()||path==fake::fail_read)return 92;
  const auto& b=fake::files.at(path);if(off<0||u64(off)>b.size())return 93;
  n=std::min<u64>(n,b.size()-off);if(fake::zero_read)n=0;else if(fake::short_read&&n>1)n=1;
  if(n)std::memcpy(out,b.data()+off,n);*got=n;return 0;
 }
};
struct Dir {std::vector<std::string> entries;std::size_t at{};
 Result Read(s64* got,std::size_t,FsDirectoryEntry* e){if(fake::fail_dir)return 94;*got=at<entries.size();if(*got)std::strcpy(e->name,entries[at++].c_str());return 0;}
};
struct Fs {
 Result OpenFile(const FsPath& p,u32,File* f){if(p.s==fake::fail_open||!fake::files.contains(p.s))return 95;f->path=p.s;++fake::opens;++fake::active;fake::max_active=std::max(fake::max_active,fake::active);return 0;}
 Result OpenDirectory(const FsPath& p,u32,Dir* d){const std::string prefix(p.s);for(const auto& [name,b]:fake::files)if(name.starts_with(prefix))d->entries.push_back(name.substr(prefix.size()));return 0;}
};
}
