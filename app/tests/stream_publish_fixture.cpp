// Execute the production forward-install orchestration with storage/crypto spies.
#include "nexus/task_progress.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;using Result=u32;
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(false)
#define R_UNLESS(c,r) do{if(!(c))return r;}while(false)
#define R_SUCCEED() return 0
#define ON_SCOPE_EXIT(...) ((void)0)
constexpr Result Result_NspInvalidHeader=51,Result_YatiNczSectionNotFound=52,Result_YatiCertNotFound=53,Result_YatiInvalidNcaSha256=54;
constexpr u8 NcmContentType_Meta=0;
void log_write(const char*,...){}
struct FsRightsId{u8 c[16]{};};
namespace keys{void parse_hex_key(u8*,const char*){}}
namespace nexus::archive{std::string Canonical(const std::string& s){return s;}}
namespace ui{struct ProgressBox{void TransferEvent(const char*){}};struct ProgressScope{ProgressScope(ProgressBox*,unsigned,unsigned){}};}
namespace source{struct Base{bool finished{};Result eof_error{};s64 minimum{};Result FinishRead(s64 n){minimum=n;if(eof_error)return eof_error;finished=true;return 0;}};}
namespace container{struct CollectionEntry{std::string name;s64 offset{},size{};};using Collections=std::vector<CollectionEntry>;}
struct NcaCollection:container::CollectionEntry{
 struct Header{u64 size{};}header;u8 type{};u8 expected_hash[32]{},source_digest[32]{};u64 expected_size{};bool has_expected_hash{};
 struct Written{u64 key{};}usb_written;
};
struct CnmtCollection:NcaCollection{std::vector<NcaCollection> ncas;};
struct TikCollection{FsRightsId rights_id;std::vector<u8> ticket,cert;};
struct Outcome{u32 installed_records{},skipped_records{};};
struct ConfigOverride{Outcome* outcome{};};
struct Requirement{std::string name;u64 size{};std::array<u8,32> hash{};};
std::vector<Requirement> required;unsigned publications{},ticket_calls{},writes{},corrupt_index=999;bool bad_size{},fail_publish{};
u64 VerificationKey(const NcaCollection& n,u64 gen){assert(n.has_expected_hash);return n.expected_size+gen;}
struct Yati{
 source::Base* source;Outcome* outcome{};u64 usb_generation=42,diagnostic_title{};
 struct Config{bool skip_if_already_installed{},convert_to_standard_crypto{},lower_master_key{},skip_nca_hash_verify{true};}config;
 Yati(ui::ProgressBox*,source::Base* s):source(s){}
 Result Setup(const ConfigOverride& c){outcome=c.outcome;return 0;}
 void Cleanup(const NcaCollection&){}
 Result ParseTicketsIntoCollection(std::vector<TikCollection>&,const container::Collections&,bool){return 0;}
 Result ReadSourceExact(void*,s64,s64){return 0;}
 Result InstallNca(std::span<TikCollection>,NcaCollection& n){
  assert(!config.skip_nca_hash_verify);++writes;auto it=std::find_if(required.begin(),required.end(),[&](const auto& r){return r.name==n.name;});assert(it!=required.end());
  n.header.size=it->size+(bad_size?1:0);std::memcpy(n.source_digest,it->hash.data(),32);
  if(size_t(it-required.begin())==corrupt_index)n.source_digest[31]^=1;return 0;
 }
 Result InstallCnmtNca(std::span<TikCollection>,CnmtCollection& meta,const container::Collections& entries){
  for(const auto& r:required){auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.name==r.name;});assert(found!=entries.end());
   NcaCollection n{*found};n.has_expected_hash=true;n.expected_size=r.size;n.type=7;memcpy(n.expected_hash,r.hash.data(),32);meta.ncas.push_back(n);
  }return 0;
 }
 Result GetLatestVersion(const CnmtCollection&,u32& version,bool& skip){version=1;skip=false;return 0;}
 Result ShouldSkip(const CnmtCollection&,bool&){return 0;}
 Result ImportTickets(std::span<TikCollection>){assert(source->finished);++ticket_calls;return 0;}
 Result RemoveInstalledNcas(const CnmtCollection&){return 0;}
 Result RegisterNcasAndPushRecord(const CnmtCollection& cnmt,u32){
  assert(source->finished);++publications;
  for(const auto& n:cnmt.ncas){assert(n.has_expected_hash&&n.expected_size==n.header.size&&n.type==7);
   assert(!memcmp(n.expected_hash,n.source_digest,32)&&n.usb_written.key==n.expected_size+42);}
  return fail_publish?99:0;
 }
};
// PRODUCTION
int main(){
 ui::ProgressBox box;
 for(bool meta_last:{false,true})for(int failure=0;failure<5;++failure){
  publications=ticket_calls=writes=0;bad_size=failure==2;fail_publish=failure==4;corrupt_index=failure==1?1:999;
  required={{"a.nca",100,{}},{"b.ncz",400,{}}};for(size_t i=0;i<required.size();++i)for(size_t j=0;j<32;++j)required[i].hash[j]=i+j;
  container::Collections files{{"base.cnmt.nca",meta_last?500:0,20},{"a.nca",100,100},{"b.ncz",200,100},{"dlc.cnmt.nca",600,20}};
  source::Base input;input.eof_error=failure==3?77:0;Outcome outcome;
  const auto rc=InstallInternalStream(&box,&input,files,{&outcome});
  assert(writes==2);
  if(!failure){assert(!rc&&publications==2&&ticket_calls==2&&outcome.installed_records==2&&input.minimum==620);}
  else if(failure==4){assert(rc==99&&publications==1&&!outcome.installed_records);}
  else{assert(rc&&publications==0&&ticket_calls==0&&!outcome.installed_records);}
 }
 std::cout<<"Actual stream publication: CNMT before/after content, expanded NCZ size, full 32-byte hash binding, all-title validation before publication, clean EOF and failed-registration counting passed\n";
}
