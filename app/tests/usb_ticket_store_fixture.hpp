#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;using Result=u32;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
#define R_TRY(call) do{const Result result_=(call);if(result_)return result_;}while(0)
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(0)
#define ON_SCOPE_EXIT(...) ((void)0)
struct FsRightsId{u8 c[16]{};};
inline u64 current_console=17,tick=1;inline Result transport_error{},write_error{},rename_error{},import_error{};
inline unsigned writes{},imports{};inline bool imported{},readback_registered=true;
inline std::map<std::string,std::vector<u8>> files;inline std::map<std::string,bool> dirs;
enum {SplConfigItem_DeviceId=8};
Result splInitialize(){return 0;}
void splExit(){}
Result splGetConfig(int,u64* id){*id=current_console;return 0;}
u64 armGetSystemTick(){return ++tick;}
// Integrity tests exercise the adapter's byte coverage and comparison, not
// libnx's SHA primitive. This deterministic fixture is not cryptography.
struct Sha256Context{u64 value{1469598103934665603ULL};};
void sha256ContextCreate(Sha256Context* h){*h={};}
void sha256ContextUpdate(Sha256Context* h,const void* data,size_t size){
    const auto* b=static_cast<const u8*>(data);while(size--){h->value^=*b++;h->value*=1099511628211ULL;}
}
void sha256ContextGetHash(Sha256Context* h,void* out){
    auto* b=static_cast<u8*>(out);for(size_t i=0;i<32;++i){b[i]=u8(h->value>>(i%8*8));}
}
namespace sphaira::es {
struct TicketData{FsRightsId rights_id;};
bool IsRightsIdValid(const FsRightsId& r){return std::any_of(std::begin(r.c),std::end(r.c),[](u8 b){return b;});}
Result GetTicketData(std::span<const u8> ticket,TicketData* out){if(ticket.size()<sizeof(TicketData))return 9;memcpy(out,ticket.data(),sizeof(*out));return 0;}
Result Initialize(){return 0;}void Exit(){}
Result ImportTicket(const void*,u64,const void*,u64){++imports;if(import_error)return import_error;imported=true;return 0;}
Result IsRightsIdCommon(const FsRightsId&,bool* out){*out=imported&&readback_registered;return 0;}
Result IsRightsIdPersonalised(const FsRightsId&,bool* out){*out=false;return 0;}
}
namespace nexus {
struct Info {u64 size{};bool directory{};};
class Client {
public:
 Result Check(u64 gen){return transport_error?transport_error:(gen==42?0:77);}
 Result Stat(u64 gen,const std::string& path,Info& out){R_TRY(Check(gen));if(dirs.contains(path)){out={0,true};return 0;}auto it=files.find(path);if(it==files.end())return MAKERESULT(140,205);out={it->second.size(),false};return 0;}
 Result Read(u64 gen,const std::string& path,u64 off,void* data,size_t size,u64& got){R_TRY(Check(gen));auto it=files.find(path);if(it==files.end())return MAKERESULT(140,205);if(off>it->second.size())return 88;got=std::min<u64>(size,it->second.size()-off);memcpy(data,it->second.data()+off,got);return 0;}
 Result CreateDirectory(u64 gen,const std::string& path){R_TRY(Check(gen));++writes;if(write_error)return write_error;dirs[path]=true;return 0;}
 Result CreateFile(u64 gen,const std::string& path,u64 size){R_TRY(Check(gen));++writes;if(write_error)return write_error;if(files.contains(path))return 13;files[path].resize(size);return 0;}
 Result Write(u64 gen,const std::string& path,u64 off,const void* data,size_t size){R_TRY(Check(gen));++writes;if(write_error)return write_error;auto& dest=files[path];if(off+size>dest.size())return 88;memcpy(dest.data()+off,data,size);return 0;}
 Result Flush(u64 gen){R_TRY(Check(gen));++writes;return write_error;}
 Result RenameNew(u64 gen,const std::string& from,const std::string& to){R_TRY(Check(gen));++writes;if(rename_error)return rename_error;if(files.contains(to))return 13;auto it=files.find(from);if(it==files.end())return 99;files.emplace(to,std::move(it->second));files.erase(it);return 0;}
};
}
