// SPDX-License-Identifier: GPL-3.0-or-later
// Physical USB reads are recorded, with synthetic NCA headers and digests.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;using Result=int;
#define R_TRY(x) do{if(auto rc=(x))return rc;}while(false)
#define R_UNLESS(x,err) do{if(!(x))return err;}while(false)
#define _nx
constexpr int KEYGEN_LIMIT=32,Result_YatiTicketNotFound=3,Result_YatiInvalidNcaSha256=4;
constexpr int NcmContentType_Meta=0,NcmContentType_Program=1,NcmContentType_Control=2;
using FsRightsId=int;
namespace keys {struct Keys{int header_key{};};}
namespace nca {
constexpr int ContentType_Meta=0,ContentType_Program=1,ContentType_Control=2;
struct Header {
    u64 size{};int content_type{},rights_id{},kaek_index{};
    bool IsValid()const{return size!=0;}
    int GetKeyGeneration()const{return 0;}
};
Result DecryptHeader(void*,const keys::Keys&,Header&){return 2;}
struct ContentHash{
    u8 digest{};
    void Update(const u8* p,size_t n,int){for(size_t i=0;i<n;i++)digest^=p[i];}
    bool Matches(const void* p,int)const{return digest==*static_cast<const u8*>(p);}
};
}
namespace ui {struct ProgressBox{Result ShouldExitResult(){return 0;}void NewTransfer(const char*){} void AddTransferBytes(u64){} void UpdateTransfer(u64,u64){}};}
namespace es{
bool ticket{};
Result GetCommonTickets(std::vector<FsRightsId>& out){if(ticket)out={9};return 0;}
Result GetPersonalisedTickets(std::vector<FsRightsId>&){return 0;}
bool IsRightsIdValid(int id){return id!=0;}
bool IsRightsIdFound(int id,const std::vector<int>& ids){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
}
using Id=std::array<u8,16>;
struct NcaCollection{Id content_id{};u64 expected_size{};int type{};nca::Header header{};bool has_expected_hash{};u8 expected_hash[32]{};};
struct Key{u64 id{};u32 version{},type{},install_type{};};
struct CnmtCollection:NcaCollection{Key key{};std::vector<NcaCollection> ncas;};
Id ExistingUsbPath(const Id& id){return id;}
namespace nexus {
constexpr int Invalid=7;
struct Info{bool directory{};u64 size{};};
std::map<Id,std::vector<u8>> disk;
std::map<Id,u64> bytes_read;
struct Client {
    Result Check(u64 g){return g==31?0:6;}
    Result Stat(u64,const Id& path,Info& res){if(!disk.contains(path))return 1;res.size=disk.at(path).size();return 0;}
    Result Read(u64,const Id& path,u64 off,void* out,u64 count,u64& got){
        const auto& file=disk.at(path);
        if(off>file.size() || count>file.size()-off)return 1;
        memcpy(out,file.data()+off,count);bytes_read[path]+=count;got=count;return 0;
    }
};
}
namespace nexusapp {bool HeaderType(int expected,int on_disk){return expected==on_disk;}}
// PRODUCTION_CHECK
struct ExistingUsbRecord {Key key{};u64 application_id{};CnmtCollection files;};
Result ExistingCollection(const ExistingUsbRecord& rec,CnmtCollection& cnmt){cnmt=rec.files;return 0;}
namespace source {struct Base{virtual Result Read(void*,s64,s64,u64*)=0;};}
namespace ncm {struct ContentStorageRecord{Key key;u32 storage_id;};}
namespace ns {Result GetApplicationRecords(u64,std::vector<ncm::ContentStorageRecord>&){return 0;}}
namespace nexus {enum class Target{Usb};namespace install{struct Diagnostic{};}}
constexpr int NcmStorageId_SdCard=5;
struct ConfigOverride{nexus::Target nexus_target{};u64 nexus_destination_generation{};nexus::install::Diagnostic* diagnostic{};bool archive_usb_tickets{};};
static int ensured{},commits{},deep_calls{};static bool reject_route=false;
struct Destination{
    Result Verify(int,const Id& id,u64 size){
        assert(size==nexus::disk.at(id).size());++ensured;return reject_route?12:0;
    }
};
struct Yati{
    Yati(ui::ProgressBox*,source::Base*){}
    int cs{},diagnostic_title{};keys::Keys keys;bool defer_postcommit{};Destination dest,*destination=&dest;
    Result Setup(const ConfigOverride& config){return config.nexus_destination_generation==31?0:1;}
    Result RegisterMetaAndPushRecord(const CnmtCollection&,u32){++commits;return 0;}
    Result RegisterNcasAndPushRecord(const CnmtCollection&,u32){++deep_calls;return 0;}
};
// PRODUCTION_PUBLISH
int main(){
    CnmtCollection cnmt;cnmt.ncas.resize(1);
    auto& payload=cnmt.ncas[0];
    for(auto* n:{static_cast<NcaCollection*>(&cnmt),&payload}){
        bool meta=n==&cnmt;const u64 size=meta?4096:8*1024*1024;
        n->type=meta?NcmContentType_Meta:NcmContentType_Program;
        nca::Header header{};header.size=size;header.content_type=n->type;header.rights_id=meta?0:9;
        std::vector<u8> encoded(size,0x6a);memcpy(encoded.data(),&header,sizeof(header));
        nca::ContentHash h;h.Update(encoded.data(),size,0);
        n->content_id[0]=h.digest;n->content_id[1]=n->type;n->expected_hash[0]=h.digest;n->has_expected_hash=!meta;n->expected_size=size;
        nexus::disk[n->content_id]=std::move(encoded);
    }
    ui::ProgressBox p;
    const auto check=[&](bool full,bool tickets,u64 generation=31){return InspectExisting(&p,generation,cnmt,{},tickets?ExistingCheck::BeforeRegistration:full?ExistingCheck::Full:ExistingCheck::QuickPc);};
    assert(!check(false,false));
    assert(nexus::bytes_read[payload.content_id]==sizeof(nca::Header));
    assert(nexus::bytes_read[cnmt.content_id]==cnmt.expected_size+sizeof(nca::Header));
    assert(check(false,true)==Result_YatiTicketNotFound);es::ticket=true;
    assert(!check(false,true));
    ExistingUsbRecord record{{},100,cnmt};
    auto publish=[&](bool quick){return PublishExistingUsbRecord(&p,31,record,0,quick,nullptr);};
    es::ticket=false;assert(publish(true)==Result_YatiTicketNotFound && !ensured && !commits);
    es::ticket=true;nexus::bytes_read.clear();
    assert(!publish(true) && ensured==2 && commits==1 && !deep_calls);
    assert(nexus::bytes_read[payload.content_id]==sizeof(nca::Header));
    reject_route=true;assert(publish(true)==12 && commits==1);reject_route=false;
    assert(!publish(false) && deep_calls==1);
    auto& bytes=nexus::disk[payload.content_id];
    const auto previous=bytes[offsetof(nca::Header,content_type)];bytes[offsetof(nca::Header,content_type)]=NcmContentType_Control;
    assert(check(false,false));bytes[offsetof(nca::Header,content_type)]=previous;
    nexus::disk[payload.content_id][bytes.size()/2]^=1;
    assert(!check(false,false) && check(true,false)==Result_YatiInvalidNcaSha256);
    nexus::disk[cnmt.content_id].back()^=1;assert(check(false,false)==Result_YatiInvalidNcaSha256);
    nexus::disk[cnmt.content_id].back()^=1;assert(check(false,false,99));
    nexus::disk[payload.content_id].pop_back();assert(check(false,false));
    nexus::disk.erase(payload.content_id);assert(check(false,false));
    std::cout<<"8 MiB synthetic game read one header in quick preflight; deep verification detected an interior change. Metadata, ticket, missing-file, length and generation checks passed.\n";
}
