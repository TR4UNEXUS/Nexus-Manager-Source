// SPDX-License-Identifier: GPL-3.0-or-later
// Host platform adapters for the extracted, unmodified production cleanup.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "nexus/nx_Policy.hpp"
#include "nexus/safety.hpp"
#include "nexus/installed_move_core.hpp"
#include "nexus/transfer_report.hpp"
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;
using s32=std::int32_t;using s64=std::int64_t;using Result=u32;
#define MAKERESULT(m,d) ((m)+((d)<<9))
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
#define R_TRY(x) do{const auto result_=(x);if(result_)return result_;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return (r);}while(0)
#define R_SUCCEED() return 0
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
template<class F>struct ScopeExit{F f;~ScopeExit(){f();}};
#define ON_SCOPE_EXIT(...) auto JOIN(scope_exit_,__LINE__)=ScopeExit{[&](){__VA_ARGS__;}}
constexpr Result Fault=123,Cancel=124,Invalid=99,Missing=125;
constexpr int NcmStorageId_BuiltInUser=4,NcmStorageId_SdCard=5,FS_MAX_PATH=0x301;
constexpr int NcmContentMetaType_Unknown=0,NcmContentMetaType_Application=0x80,NcmContentMetaType_DataPatch=0x84;
constexpr int NcmContentInstallType_Unknown=0xFF;
struct NcmContentId{u8 c[16]{};};
struct NcmContentInfo{NcmContentId content_id;u32 size_low{};u8 size_high{},attr{},content_type{},id_offset{};};
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{},install_type{},padding[2]{};};
struct NcmContentMetaHeader{u16 extended_header_size{},content_count{},content_meta_count{};u8 attributes{},storage_id{};};
struct NcmContentMetaInfo{u8 bytes[16];};
struct NcmApplicationMetaExtendedHeader{u64 patch;u32 system,application;};
struct NcmContentStorage{nexus::Target target{};};
struct NcmContentMetaDatabase{int storage{};};
struct Record{NcmContentMetaKey key;std::vector<u8> bytes;};
static std::vector<Record> database;
static std::map<u8,u64> sd_files,usb_files;
static std::vector<u8> deleted;
static std::map<std::string,Result> failures;
static std::string current_stage,fail_stage,cancel_stage,disconnect_stage,read_error_stage;
static bool cancelled{},connected=true,published=true,missing_destination{};
static unsigned delete_calls{},registration_calls{},db_reads{},ns_refs{},checks{},db_max_request{};
static u64 media_generation=7;
static std::vector<std::string> stages;
static unsigned asserts;
#define CHECK(x) do{++asserts;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(0)
static void Reset(){database.clear();sd_files.clear();usb_files.clear();deleted.clear();failures.clear();stages.clear();
    current_stage=fail_stage=cancel_stage=disconnect_stage=read_error_stage="";
    cancelled=false;connected=published=true;missing_destination=false;delete_calls=registration_calls=db_reads=ns_refs=checks=db_max_request=0;media_generation=7;}
static Result StageFailure(){return !fail_stage.empty()&&current_stage==fail_stage?Fault:0;}
static Record* Find(const NcmContentMetaKey& key){for(auto& r:database)if(nexus::SameMetaKey(r.key,key))return &r;return nullptr;}
void ncmContentInfoSizeToU64(const NcmContentInfo* i,u64* out){*out=i->size_low|(u64(i->size_high)<<32);}
void ncmContentMetaDatabaseClose(NcmContentMetaDatabase*){}
void ncmContentStorageClose(NcmContentStorage*){}
Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase* db,int storage){db->storage=storage;return StageFailure();}
Result ncmContentMetaDatabaseHas(NcmContentMetaDatabase*,bool* out,const NcmContentMetaKey* key){*out=Find(*key)!=nullptr;return StageFailure();}
Result ncmContentMetaDatabaseGetSize(NcmContentMetaDatabase*,u64* out,const NcmContentMetaKey* key){
    ++db_reads;if(!read_error_stage.empty()&&current_stage==read_error_stage)return Fault;auto r=Find(*key);if(!r)return Missing;*out=r->bytes.size();return StageFailure();}
Result ncmContentMetaDatabaseGet(NcmContentMetaDatabase*,const NcmContentMetaKey* key,u64* got,void* out,size_t capacity){
    ++db_reads;db_max_request=std::max(db_max_request,unsigned(capacity));if(!read_error_stage.empty()&&current_stage==read_error_stage)return Fault;
    auto r=Find(*key);if(!r)return Missing;*got=std::min(capacity,r->bytes.size());std::memcpy(out,r->bytes.data(),*got);return StageFailure();}
Result ncmContentMetaDatabaseListContentInfo(NcmContentMetaDatabase*,s32* count,NcmContentInfo* out,size_t capacity,const NcmContentMetaKey* key,s32 offset){
    ++db_reads;auto r=Find(*key);if(!r||r->bytes.size()<8||offset<0)return Missing;
    NcmContentMetaHeader h{};std::memcpy(&h,r->bytes.data(),8);*count=0;
    for(size_t index=size_t(offset);index<h.content_count&&size_t(*count)<capacity;++index){
        const auto pos=8+size_t(h.extended_header_size)+index*sizeof(NcmContentInfo);
        if(pos>r->bytes.size()||sizeof(NcmContentInfo)>r->bytes.size()-pos)return Invalid;
        std::memcpy(out+*count,r->bytes.data()+pos,sizeof(NcmContentInfo));++*count;
    }
    return StageFailure();
}
Result ncmContentMetaDatabaseList(NcmContentMetaDatabase*,s32* total,s32* count,NcmContentMetaKey* out,size_t capacity,int,u64,u64,u64,int){
    *total=s32(database.size());*count=s32(std::min(capacity,database.size()));for(s32 i=0;i<*count;++i)out[i]=database[size_t(i)].key;return StageFailure();}
Result ncmContentMetaDatabaseRemove(NcmContentMetaDatabase*,const NcmContentMetaKey*){return Fault;}
Result ncmContentMetaDatabaseCommit(NcmContentMetaDatabase*){return Fault;}
Result ncmContentStorageHas(NcmContentStorage* cs,bool* out,const NcmContentId* id){CHECK(cs->target==nexus::Target::Sd);*out=sd_files.contains(id->c[0]);return StageFailure();}
Result ncmContentStorageGetSizeFromContentId(NcmContentStorage* cs,s64* out,const NcmContentId* id){CHECK(cs->target==nexus::Target::Sd);if(!sd_files.contains(id->c[0]))return Missing;*out=s64(sd_files.at(id->c[0]));return StageFailure();}
Result ncmContentStorageGetPath(NcmContentStorage*,char*,size_t,const NcmContentId*){return Fault;}
Result ncmContentStorageDelete(NcmContentStorage* cs,const NcmContentId* id){
    CHECK(cs->target==nexus::Target::Sd);CHECK(connected&&published&&!cancelled);CHECK(registration_calls>=2);
    ++delete_calls;if(StageFailure())return Fault;CHECK(sd_files.erase(id->c[0])==1);deleted.push_back(id->c[0]);return 0;}
namespace sphaira::ncm {
struct ContentStorageRecord{NcmContentMetaKey key;int storage_id{};};
Result GetContentInfos(NcmContentMetaDatabase* db,const NcmContentMetaKey* key,std::vector<NcmContentInfo>& infos){
    auto r=Find(*key);if(!r||r->bytes.size()<8)return Invalid;NcmContentMetaHeader h{};std::memcpy(&h,r->bytes.data(),8);
    infos.resize(h.content_count);s32 count{};R_TRY(ncmContentMetaDatabaseListContentInfo(db,&count,infos.data(),infos.size(),key,0));infos.resize(size_t(count));return 0;
}
}
namespace sphaira::ns {
Result Initialize(){++ns_refs;return StageFailure();}void Exit(){CHECK(ns_refs>0);--ns_refs;}
Result GetApplicationRecords(u64 app,std::vector<ncm::ContentStorageRecord>& out){out.clear();if(published)for(const auto& r:database)if(r.key.id==app)out.push_back({r.key,NcmStorageId_SdCard});return StageFailure();}
Result InvalidateApplicationControlCache(u64){return StageFailure();}
}
namespace nexus {
constexpr Result Invalid=::Invalid,Unavailable=126;
int LogicalStorage(Target t){return t==Target::Internal?NcmStorageId_BuiltInUser:NcmStorageId_SdCard;}
Result CheckUsbMutationAllowed(){return StageFailure();}
Result EnsureUsbContentRegistered(u64 generation,const NcmContentId& id,s64 size){++registration_calls;
    if(!connected||generation!=media_generation)return Unavailable;
    if(!usb_files.contains(id.c[0])||usb_files.at(id.c[0])!=u64(size))return Invalid;return StageFailure();}
void Log(const char* tag,Result result=0,u64=0){
    if(!result&&std::string(tag).starts_with("remove-")){
        current_stage=tag;stages.push_back(tag);if(current_stage==cancel_stage)cancelled=true;if(current_stage==disconnect_stage)connected=false;
    }
}
struct Info{u64 size{};u32 cluster{};bool directory{};};
class Client{public:
    Result Check(u64 generation){++checks;return connected&&generation==media_generation?StageFailure():Unavailable;}
    Result Stat(u64,const std::string& path,Info& out){const auto id=u8(std::stoul(path.substr(1),nullptr,16));
        if(!usb_files.contains(id)||missing_destination)return MAKERESULT(140,205);out.size=usb_files.at(id);return StageFailure();}
    Result DeleteRegistered(u64,const std::string&,u64){return Fault;}
};
}
namespace sphaira::ui {struct ProgressBox{
    Result ShouldExitResult(){++checks;return cancelled?Cancel:0;}
    void NewTransfer(const std::string&){}void SetActionName(const std::string&){}
};}
namespace sphaira::nexusapp {
using nexus::Target;
constexpr Result Invalid=nexus::Invalid;
Result OpenStorage(Target target,NcmContentStorage& cs,u64* generation){cs.target=target;*generation=target==Target::Usb?media_generation:0;return StageFailure();}
std::string HexId(const void* ptr,size_t size=16){std::string out;const auto* bytes=static_cast<const u8*>(ptr);for(size_t i=0;i<size;++i){char b[3];std::snprintf(b,sizeof(b),"%02x",bytes[i]);out+=b;}return out;}
std::string UsbContentPath(const NcmContentId& id){return "/"+HexId(&id.c[0],1);}
struct TransferProgress{void Finish(unsigned,const std::string&){};};
// PRODUCTION_STORAGE
Result VerifyPair(ui::ProgressBox*,Target,Storage&,Storage&,const NcmContentInfo&){return Fault;}
// PRODUCTION_META_AND_CLEANUP
}
