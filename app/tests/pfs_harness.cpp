#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <algorithm>
#include <set>
#include <map>
#include <array>
#include <span>
#include <string>
#include <vector>
#include "nexus/content_compat_core.hpp"
#include "yati/source/read_exact.hpp"
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t; using s64=int64_t;
using Result=unsigned;
constexpr Result Result_NspInvalidHeader=91,Result_NspBadMagic=92;
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
#define R_TRY(c) do{auto r=(c);if(r)return r;}while(0)
#define R_SUCCEED() return 0
template<class... T> void log_write(const char*,T...){}
namespace sphaira::yati::source {
using ReadCallback=Result(*)(void*,s64,s64,u64*);
struct Base {
 const u8* bytes{};size_t size{};int fault{};size_t calls{},payload_calls{};
 ReadCallback reader{};
 Result Read(void* dst,s64 off,s64 count,u64* got){
  ++calls;
  if(calls>3)++payload_calls;
  if(fault==1)return 87;
  if(fault==3 && calls>3)return 87;
  if(reader)return reader(dst,off,count,got);
  if(off<0||count<0||u64(off)>size||u64(count)>size-off)return 88;
  std::memcpy(dst,bytes+off,count);*got=count-((fault==2 || (fault==4 && calls>3)) && count>0);return 0;
 }
};
}
namespace sphaira::yati::container {
struct CollectionEntry {std::string name;s64 offset{},size{};};
using Collections=std::vector<CollectionEntry>;
struct Nsp {
 source::Base* m_source{};s64 m_source_size{},m_data_offset{};
 Result GetCollections(Collections&);
 Result GetCollections(Collections&,s64);
 Result GetCollections(Collections&,s64,s64);
};
}
// PRODUCTION_PFS_READER
static sphaira::yati::container::Collections entries;
static size_t payload_calls;
extern "C" __declspec(dllexport) int ParsePfs(const u8* data,size_t size,s64 declared,s64 off,int fault) {
 sphaira::yati::source::Base source{data,size,fault};
 sphaira::yati::container::Nsp parser{&source,declared,0};
 entries={{"stale",1,1}};
 const auto result=parser.GetCollections(entries,off,declared);
 payload_calls=source.payload_calls;
 if(result){assert(entries.empty());return -static_cast<int>(result);}
 return 1+entries.size();
}
extern "C" __declspec(dllexport) const char* PfsName(size_t index){return entries.at(index).name.c_str();}
extern "C" __declspec(dllexport) u64 PfsOffset(size_t index){return entries.at(index).offset;}
extern "C" __declspec(dllexport) u64 PfsSize(size_t index){return entries.at(index).size;}
extern "C" __declspec(dllexport) size_t PfsPayloadCalls(){return payload_calls;}
extern "C" __declspec(dllexport) int ParsePfsReader(sphaira::yati::source::ReadCallback reader,s64 declared) {
 sphaira::yati::source::Base source{};source.reader=reader;
 sphaira::yati::container::Nsp parser{&source,declared,0};
 entries={{"stale",1,1}};
 const auto result=parser.GetCollections(entries,0,declared);
 if(result){assert(entries.empty());return -static_cast<int>(result);}
 return 1+entries.size();
}
