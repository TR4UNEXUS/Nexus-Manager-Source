#include "nexus/game_cleanup_core.hpp"
#include "nexus/safety.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <span>
#include <set>
#include <tuple>
#include <vector>
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;using s32=std::int32_t;using Result=u32;
constexpr Result Invalid=99,Fault=100;
#define R_TRY(x) {auto rc_=(x);if(rc_)return rc_;}
#define R_UNLESS(x,r) {if(!(x))return r;}
constexpr int NcmContentMetaType_Unknown=0,NcmContentMetaType_Application=0x80,NcmContentMetaType_Patch=0x81,NcmContentMetaType_AddOnContent=0x82,NcmContentMetaType_DataPatch=0x84,NcmContentInstallType_Unknown=7;
void CleanupStage(const char*){}
struct NcmContentId{u8 c[16]{};};
struct NcmContentInfo{NcmContentId content_id;u8 rest[8]{};};
struct NcmContentMetaHeader{u16 extended_header_size{},content_count{},content_meta_count{};u8 attributes{},storage_id{};};
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{},install_type{};};
struct Row{NcmContentMetaKey key;std::vector<u8> raw;};
std::vector<Row> rows;bool truncated_list{},short_read{},different_infos{},fail_io{},cancelled{};unsigned lists{},info_calls{};
void Reset(){rows.clear();truncated_list=short_read=different_infos=fail_io=cancelled=false;lists=info_calls=0;}
Row& Find(const NcmContentMetaKey* key){return *std::find_if(rows.begin(),rows.end(),[&](const auto& r){return nexus::SameMetaKey(r.key,*key);});}
void sha256CalculateHash(void* out,const void* data,size_t size){ // digest isn't the subject of this parser fixture
    auto* h=static_cast<u8*>(out);memset(h,0,32);const auto* b=static_cast<const u8*>(data);for(size_t n=0;n<size;++n)h[n%32]^=b[n];
}
Result ncmContentMetaDatabaseList(int*,s32* total,s32* count,NcmContentMetaKey* out,size_t capacity,int,u64,u64,u64,int){
    assert(out&&capacity);if(fail_io)return Fault;++lists;*total=rows.size();*count=std::min(capacity,rows.size());for(s32 i=0;i<*count;++i)out[i]=rows[i].key;if(truncated_list&&*count)--*count;return 0;
}
Result ncmContentMetaDatabaseGetSize(int*,u64* size,const NcmContentMetaKey* key){*size=Find(key).raw.size();return fail_io?Fault:0;}
Result ncmContentMetaDatabaseGet(int*,const NcmContentMetaKey* key,u64* actual,void* out,size_t capacity){auto& r=Find(key);assert(capacity==r.raw.size());memcpy(out,r.raw.data(),capacity);*actual=capacity-(short_read?1:0);return fail_io?Fault:0;}
Result ncmContentMetaDatabaseListContentInfo(int*,s32* count,NcmContentInfo* out,size_t capacity,const NcmContentMetaKey* key,s32){
    ++info_calls;auto& r=Find(key);NcmContentMetaHeader h{};memcpy(&h,r.raw.data(),sizeof(h));assert(capacity==h.content_count);*count=capacity;memcpy(out,r.raw.data()+8+h.extended_header_size,capacity*sizeof(*out));if(different_infos&&capacity)out[0].content_id.c[0]^=0xff;return fail_io?Fault:0;
}
namespace core=nexus::cleanup;using Id=core::Id;using Digest=std::array<u8,32>;
Id Identity(const NcmContentId& id){Id out;memcpy(out.data(),&id,16);return out;}
struct Session{int db{};Result Check(){return cancelled?Fault:0;}};
// RECORD_PRODUCTION
struct Inventory{std::vector<Record> records;std::vector<core::Reference> references;};
constexpr size_t MaxReferences=1048576;
// PARSER_PRODUCTION
Row Make(u8 type,u64 parent){Row r;r.key={parent+type,2,type,0};NcmContentMetaHeader h{u16(type==0x84?16:8),2,0,0,0};r.raw.resize(8+h.extended_header_size+2*sizeof(NcmContentInfo));memcpy(r.raw.data(),&h,8);memcpy(r.raw.data()+8+(type==0x84?8:0),&parent,8);
    for(u8 i=0;i<2;++i){NcmContentInfo info;info.content_id.c[0]=i+1;memcpy(r.raw.data()+8+h.extended_header_size+i*sizeof(info),&info,sizeof(info));}return r;}
int main(){Session s;Inventory out;constexpr u64 app=0x0100123400000000;
    Reset();assert(!Database(s,out)&&out.records.empty()&&lists==1);
    Reset();out={};for(unsigned i=0;i<300;++i)rows.push_back(Make(0x80,app+0x1000*i));assert(!Database(s,out)&&out.records.size()==300&&lists==2);out={};
    Reset();for(auto type:{0x80,0x81,0x82,0x84})rows.push_back(Make(type,app));assert(!Database(s,out)&&out.records.size()==4&&out.references.size()==4);
    assert(out.records[0].application==app+0x80);for(size_t i=1;i<4;++i)assert(out.records[i].application==app&&out.records[i].ids.size()==2);
    for(unsigned fault=0;fault<9;++fault){Reset();out={};rows.push_back(Make(0x81,app));
        if(fault==0)truncated_list=true;if(fault==1)short_read=true;if(fault==2)different_infos=true;if(fault==3)fail_io=true;if(fault==4)cancelled=true;
        if(fault==5)rows[0].raw.resize(7);if(fault==6)rows[0].raw[2]=255;
        if(fault==7)memcpy(rows[0].raw.data()+40,rows[0].raw.data()+16,24);if(fault==8)rows.push_back(rows[0]);
        assert(Database(s,out));
    }
    Reset();out={};auto empty=Make(0x80,app);empty.raw.resize(16);empty.raw[2]=0;rows.push_back(empty);assert(!Database(s,out)&&out.records.size()==1&&!info_calls);
    std::vector<u8> truncated(8);NcmContentMetaKey key{app,1,0x84,0};assert(!Parent(key,truncated));
    std::cout<<"Production content inventory parser: family IDs, malformed/truncated/duplicate metadata, short reads, inconsistent content tables, cancellation and empty tables passed\n";
}
