#include "nexus/localize.hpp"
#include "nexus/usb_import_core.hpp"
#include <cassert>
#include <iostream>
#include <tuple>
#include <cstring>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;
using Result=u32;
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(0)
#define R_TRY(call) do{const auto result_=(call);if(result_)return result_;}while(0)
#define ON_SCOPE_EXIT(...) ((void)0)
using nexus::repair::Image;using nexus::repair::Snapshot;using nexus::recovery::Bytes;
namespace core=nexus::usbimport;
namespace nexus {inline constexpr Result Invalid=123;}
struct SecmonArgs{u64 X[8]{};};
struct SystemReply {u32 magic{0x30534645},type{},id{},fs_version{1};u64 start_sector{};} system_reply;
char system_paths[256]{};u64 system_device=555,smc_result{};Result spl_error{};
struct FsDeviceOperator {bool active{};};
std::array<u8,16> system_cid{1,2,3,4};Result device_open_error{},cid_error{};unsigned device_handles{};
Result fsOpenDeviceOperator(FsDeviceOperator* device){if(device_open_error)return device_open_error;device->active=true;++device_handles;return 0;}
Result fsDeviceOperatorGetSdCardCid(FsDeviceOperator* device,void* out,size_t buffer_size,std::int64_t size){assert(device->active&&buffer_size==16&&size==16);if(cid_error)return cid_error;memcpy(out,system_cid.data(),16);return 0;}
void fsDeviceOperatorClose(FsDeviceOperator* device){assert(device->active&&device_handles);device->active=false;--device_handles;}
inline constexpr int SplConfigItem_DeviceId=8;
void svcCallSecureMonitor(SecmonArgs* args){assert(args->X[0]==0xF0000404&&args->X[1]==0&&args->X[2]%4096==0);memcpy(reinterpret_cast<void*>(args->X[2]),system_paths,sizeof(system_paths));memcpy(&args->X[1],&system_reply,sizeof(system_reply));args->X[0]=smc_result;}
Result splInitialize(){return spl_error;}void splExit(){}
Result splGetConfig(int,u64* out){*out=system_device;return 0;}
// Verify identity input coverage/canonicalisation; libnx SHA itself is not
// replaced in production and is not the subject of these adapter tests.
void sha256CalculateHash(void* out,const void* data,size_t size){u64 hash=1469598103934665603ULL;const auto* at=static_cast<const u8*>(data);while(size--){hash^=*at++;hash*=1099511628211ULL;}for(unsigned i=0;i<32;++i)static_cast<u8*>(out)[i]=hash>>(8*(i%8));}
// SYSTEM_IDENTITY_PRODUCTION
struct NcmContentMetaKey {u64 id{};u32 version{};u8 type{},install_type{},padding[2]{};};
namespace ncm {struct ContentStorageRecord{NcmContentMetaKey key{};u8 storage_id{},padding[7]{};};}
namespace nexus {bool SameMetaKey(const NcmContentMetaKey& a,const NcmContentMetaKey& b){return a.id==b.id&&a.version==b.version&&a.type==b.type&&a.install_type==b.install_type;}}
// KEY_RECORD_PRODUCTION
namespace ns {
inline constexpr u8 ApplicationRecordType_Running=0,ApplicationRecordType_Downloading=4;
Image current;u8 event{};Result error{};int pushes{};
Result PushApplicationRecordWithType(u64,const ncm::ContentStorageRecord* records,size_t count,u8 value){
    ++pushes;if(error)return error;std::vector<ncm::ContentStorageRecord> rows(records,records+count);current={true,Records(rows)};event=value;return 0;
}
Result DeleteApplicationRecord(u64){++pushes;if(error)return error;current={};return 0;}
}
struct HomeDisk {
    const char* step{};
    bool Ok(Result r){return !r;}
    bool HomeRead(u64,Image& out,u8* event){out=ns::current;if(event)*event=ns::event;return true;}
    // HOME_WRITE_PRODUCTION
};
struct FinishDisk {
    const char* step{};
    std::map<std::string,Image> files;Result rc{};int commits{},renames{},reads{};
    int fail_commit{},fail_rename{},fail_read{};
    bool Commit(){if(++commits==fail_commit){rc=1;return false;}return true;}
    bool Read(const std::string& name,Image& out){if(++reads==fail_read){rc=2;return false;}out=files[name];return true;}
    bool Rename(const std::string& from,const std::string& to){
        if(++renames==fail_rename){rc=3;return false;}
        if(!files[from].exists||files[to].exists){rc=4;return false;}files[to]=files[from];files[from]={};return true;
    }
    bool WriteNew(const std::string& name,const Bytes& data){if(files[name].exists){rc=5;return false;}files[name]={true,data};return true;}
    // FINISH_PRODUCTION
};
struct ActivationDisk {
    std::array<u8,32> system{};bool readable{true};unsigned writes{};std::map<std::string,Image> files;
    bool SystemMatches(const core::Journal& j){return readable&&system==j.system;}
    bool Check(){return true;}
    bool Read(const std::string& name,Image& out){out=files[name];return true;}
    bool Begin(const std::string&){++writes;return true;}
    bool WriteNew(const std::string& name,const Bytes& bytes){++writes;if(files[name].exists)return false;files[name]={true,bytes};return true;}
    bool Commit(){++writes;return true;}
    bool Rename(const std::string& from,const std::string& to){++writes;if(!files[from].exists||files[to].exists)return false;files[to]=files[from];files[from]={};return true;}
};
bool Unchanged(ActivationDisk&,const core::Journal&){return true;}
// ACTIVATE_PRODUCTION
struct Fake {
    Snapshot indexes;std::map<core::Key,Image> metadata;Image home;
    std::array<u8,32> system{};bool system_readable{true};
    bool pending{true};int fail{},step{},writes{};bool fail_after{};
    bool Pass(){return ++step!=fail;}
    bool SystemMatches(const core::Journal& j){return system_readable&&system==j.system;}
    bool Indexes(Snapshot& out){out=indexes;return true;}
    bool MetaRead(const core::Key& key,Image& out){out=metadata[key];return true;}
    bool HomeRead(u64,Image& out){out=home;return true;}
    template<class F>bool Mutate(F f){++writes;if(!fail_after&&!Pass())return false;f();return !fail_after||Pass();}
    bool HomeWrite(u64,const Image& f,u8){return Mutate([&]{home=f;});}
    bool MetaWrite(const core::Key& key,const Image& f){return Mutate([&]{metadata[key]=f;});}
    bool IndexesRestore(const core::Journal& j){return Mutate([&]{indexes=j.before;});}
    bool Finish(const core::Journal&,bool){++writes;if(!Pass())return false;pending=false;return true;}
    // HOME_INTERMEDIATE_PRODUCTION
};
ncm::ContentStorageRecord Record(u64 id,u8 type,u32 version=1,u8 storage=5){ncm::ContentStorageRecord r{};r.key.id=id;r.key.type=type;r.key.version=version;r.storage_id=storage;return r;}
core::Journal Journal(){
    core::Journal j;j.nonce=123;j.application=100;j.system[0]=9;
    nexus::repair::Indexes indexes;indexes.titles[900]=0;j.before=nexus::repair::Encode(indexes);
    indexes.titles[100]=0;j.after=nexus::repair::Encode(indexes);
    j.metadata={{{100,1,0x80,0},{},{true,Bytes(8,1)}},{{101,1,0x81,0},{true,Bytes(8,2)},{true,Bytes(8,3)}}};
    j.home_before={true,Records({Record(900,0x82),Record(101,0x81,0)})};
    j.home_after={true,Records({Record(900,0x82),Record(100,0x80),Record(101,0x81)})};
    return j;
}
Fake State(const core::Journal& j){Fake f;f.system=j.system;f.indexes=j.after;f.home=j.home_after;for(const auto& m:j.metadata)f.metadata[m.key]=m.after;return f;}
void Recrc(Bytes& b){const auto crc=nexus::repair::Crc(b,b.size()-4);for(unsigned i=0;i<4;++i)b[b.size()-4+i]=crc>>(8*i);}
int main(){
    auto j=Journal();const auto original=core::Encode(j);core::Journal decoded;
    assert(core::Decode(original,decoded));assert(core::Encode(decoded)==original);
    assert(nexus::repair::Get32(original,4)==2);
    {auto unbound=j;unbound.system={};assert(!core::Decode(core::Encode(unbound),decoded));}
    {auto legacy=original;legacy[4]=1;Recrc(legacy);assert(!core::Decode(legacy,decoded));}
    {auto f=State(j);f.system[1]=7;assert(!core::Restore(f,j)&&f.writes==0&&f.pending);}
    {auto f=State(j);f.system_readable=false;assert(!core::Restore(f,j)&&f.writes==0&&f.pending);}
    {ActivationDisk disk;assert(!Activate(disk,j)&&disk.writes==0);disk.system=j.system;disk.readable=false;assert(!Activate(disk,j)&&disk.writes==0);disk.readable=true;assert(Activate(disk,j)&&disk.files[core::Pending].exists);}
    // Actual SMC adapter validates the register ABI and termination, binds the
    // console+sys/emuMMC location, and stays stable across firmware upgrades.
    std::array<u8,32> system_id{},changed{};assert(SystemIdentity(system_id)==0);
    // Hekate's real sysMMC boot leaves a fully zero NONE config. The old
    // magic-only check rejected this with 0x1759 before any import writes.
    // Its canonical identity must remain identical to the accepted EFS0 NONE
    // form, so existing sysMMC recovery journals remain usable.
    {
        const auto tagged=system_reply;const auto sysmmc_id=system_id;
        system_reply={0,0,0,0,0};assert(SystemIdentity(changed)==0&&changed==sysmmc_id);
        system_reply.magic=1;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.magic=0;
        system_reply.type=1;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.type=0;
        system_reply.type=2;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.type=0;
        system_reply.id=1;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.id=0;
        system_reply.fs_version=1;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.fs_version=0;
        system_reply.start_sector=1;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.start_sector=0;
        for(size_t offset:{size_t(0),size_t(127),size_t(128),size_t(255)}){
            system_paths[offset]='x';assert(SystemIdentity(changed)==nexus::Invalid);system_paths[offset]=0;
        }
        const auto cid=system_cid;system_cid={};assert(SystemIdentity(changed)==nexus::Invalid);system_cid=cid;
        system_device=0;assert(SystemIdentity(changed)==nexus::Invalid);system_device=555;
        system_reply=tagged;system_reply.type=1;system_reply.start_sector=1234;
        assert(SystemIdentity(changed)==0&&changed!=sysmmc_id);
        system_reply=tagged;assert(SystemIdentity(changed)==0&&changed==sysmmc_id&&device_handles==0);
    }
    ++system_reply.fs_version;assert(SystemIdentity(changed)==0&&changed==system_id);
    ++system_device;assert(SystemIdentity(changed)==0&&changed!=system_id);--system_device;
    ++system_cid[5];assert(SystemIdentity(changed)==0&&changed!=system_id&&device_handles==0);--system_cid[5];
    {const auto saved=system_cid;system_cid={};assert(SystemIdentity(changed)==nexus::Invalid&&device_handles==0);system_cid=saved;}
    device_open_error=781;assert(SystemIdentity(changed)==781&&device_handles==0);device_open_error=0;
    cid_error=782;assert(SystemIdentity(changed)==782&&device_handles==0);cid_error=0;
    ++system_reply.id;assert(SystemIdentity(changed)==0&&changed!=system_id);--system_reply.id;
    strcpy(system_paths+128,"emuMMC/RAW1/Nintendo");assert(SystemIdentity(changed)==0&&changed!=system_id);
    system_reply.type=1;system_reply.start_sector=1234;assert(SystemIdentity(system_id)==0);
    ++system_reply.start_sector;assert(SystemIdentity(changed)==0&&changed!=system_id);
    system_reply.start_sector=0;assert(SystemIdentity(changed)==nexus::Invalid);
    system_reply.type=2;strcpy(system_paths,"emuMMC/SD00");assert(SystemIdentity(system_id)==0);
    strcpy(system_paths,"emuMMC/SD01");assert(SystemIdentity(changed)==0&&changed!=system_id);
    system_paths[0]=0;assert(SystemIdentity(changed)==nexus::Invalid);
    memset(system_paths,'x',128);assert(SystemIdentity(changed)==nexus::Invalid);memset(system_paths,0,sizeof(system_paths));
    system_reply.type=0;memset(system_paths+128,'x',128);assert(SystemIdentity(changed)==nexus::Invalid);memset(system_paths,0,sizeof(system_paths));
    system_reply.type=3;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.type=0;
    system_reply.magic=0;assert(SystemIdentity(changed)==nexus::Invalid);system_reply.magic=0x30534645;
    smc_result=1;assert(SystemIdentity(changed)==nexus::Invalid);smc_result=0;
    system_device=0;assert(SystemIdentity(changed)==nexus::Invalid);system_device=555;
    spl_error=456;assert(SystemIdentity(changed)==456);spl_error=0;
    // CRC, bounds, trailing data and duplicate keys are never repair plans.
    for(size_t i=0;i<original.size();++i){auto corrupt=original;corrupt[i]^=1;assert(!core::Decode(corrupt,decoded));}
    for(size_t n=0;n<original.size();++n){auto truncated=original;truncated.resize(n);assert(!core::Decode(truncated,decoded));}
    auto excessive=original;excessive[28]=0xff;excessive[29]=0xff;Recrc(excessive);assert(!core::Decode(excessive,decoded));
    auto absent=original;absent[64]=0;Recrc(absent);assert(!core::Decode(absent,decoded));
    auto duplicate=j;duplicate.metadata.push_back(duplicate.metadata.front());assert(!core::Decode(core::Encode(duplicate),decoded));
    auto too_big=j;too_big.metadata[0].after.data.resize(core::MaxMeta+1);assert(!core::Decode(core::Encode(too_big),decoded));
    auto bad_after=j;bad_after.home_after.exists=false;bad_after.home_after.data.clear();assert(!core::Decode(core::Encode(bad_after),decoded));
    auto tail=original;tail.insert(tail.end()-4,0);Recrc(tail);assert(!core::Decode(tail,decoded));
    for(size_t offset:{size_t(12),size_t(13),size_t(14),size_t(16),size_t(17)}){auto bad=j;bad.home_after.data[offset]=offset==12?0x83:1;assert(!core::Decode(core::Encode(bad),decoded));}
    {auto bad=j;auto rows=Records(bad.home_after.data);rows.push_back(rows.front());bad.home_after.data=Records(rows);assert(!core::Decode(core::Encode(bad),decoded));}
    {auto bad=j;auto extra=bad.metadata[0];extra.key.version++;bad.metadata.push_back(extra);assert(!core::Decode(core::Encode(bad),decoded));}
    {auto bad=j;bad.metadata[0].key.install=1;assert(!core::Decode(core::Encode(bad),decoded));}
    // Exercise the actual final-marker adapter, including commit-after-rename
    // failure, failure to rename back, and unreadable marker/destination.
    for(bool success:{false,true}){
        FinishDisk ok;ok.files[core::Pending]={true,original};assert(ok.Finish(j,success));assert(!ok.files[core::Pending].exists);
        for(int rename_failure:{0,2})for(int read_failure:{0,1,2}){
            FinishDisk disk;disk.files[core::Pending]={true,original};disk.fail_commit=1;disk.fail_rename=rename_failure;disk.fail_read=read_failure;
            assert(!disk.Finish(j,success));assert(disk.rc==1);assert((disk.files[core::Pending]==Image{true,original}));
        }
        FinishDisk refused;refused.files[core::Pending]={true,original};refused.fail_rename=1;assert(!refused.Finish(j,success)&&refused.files[core::Pending].exists);
    }
    // HOME rollback restores the original event even when records already
    // match; a matching byte image alone does not finish the restoration.
    {HomeDisk disk;ns::current=j.home_before;ns::event=3;ns::pushes=0;assert(disk.HomeWrite(j.application,j.home_before,6)&&ns::event==6&&ns::pushes==1);assert(disk.HomeWrite(j.application,j.home_before,6)&&ns::pushes==1);ns::error=99;assert(!disk.HomeWrite(j.application,j.home_before,7));ns::error=0;assert(disk.HomeWrite(j.application,{},6)&&!ns::current.exists);}
    for(u8 event:{ns::ApplicationRecordType_Running,ns::ApplicationRecordType_Downloading}){HomeDisk disk;ns::current=j.home_after;ns::event=event;ns::pushes=0;assert(!disk.HomeWrite(j.application,j.home_before,6)&&ns::pushes==0&&ns::current==j.home_after);assert(!disk.HomeWrite(j.application,j.home_after,event)&&ns::pushes==0);}
    // Roll back every valid combination of before/after/rename-gap indexes
    // and partial metadata publications, without changing the journal.
    for(unsigned indexes=0;indexes<27;++indexes)for(unsigned meta=0;meta<4;++meta){
        auto f=State(j);auto state=indexes;
        for(size_t i=0;i<3;++i){const auto n=state%3;state/=3;f.indexes[i]=n==0?j.before[i]:n==1?j.after[i]:Image{};}
        for(size_t i=0;i<2;++i)f.metadata[j.metadata[i].key]=(meta&(1<<i))?j.metadata[i].before:j.metadata[i].after;
        assert(core::CanRestore(f,j));assert(core::Restore(f,j));assert(!f.pending&&f.indexes==j.before&&f.home==j.home_before);
        for(const auto& m:j.metadata)assert(f.metadata[m.key]==m.before);
        assert(core::Encode(j)==original);
    }
    // Interruption before and after every rollback write is retryable. The
    // pending marker is only removed after all state is back at the snapshot.
    for(bool after:{false,true})for(int failure=1;failure<=5;++failure){
        auto f=State(j);f.fail=failure;f.fail_after=after;
        assert(!core::Restore(f,j));assert(f.pending);f.fail=0;f.step=0;f.fail_after=false;
        assert(core::Restore(f,j));assert(!f.pending&&f.indexes==j.before&&f.home==j.home_before);
    }
    // Unrelated changes are preserved, and detected before the first write.
    {auto f=State(j);nexus::repair::Indexes extra;nexus::repair::Decode(f.indexes,extra);extra.titles[555]=0;f.indexes=nexus::repair::Encode(extra);assert(!core::Restore(f,j)&&f.writes==0&&f.pending);}
    {auto f=State(j);f.metadata[j.metadata[0].key]={true,Bytes(8,99)};assert(!core::Restore(f,j)&&f.writes==0&&f.pending);}
    {auto f=State(j);f.home={true,Records({Record(555,0x80)})};assert(!core::Restore(f,j)&&f.writes==0&&f.pending);}
    // Actual coordinator NS intermediate rule: each changed title is either
    // the original complete group or the planned complete group; all unrelated
    // records remain present. No union of old/new versions or partial group.
    {auto f=State(j);f.home={true,Records({Record(900,0x82),Record(100,0x80),Record(101,0x81,0)})};assert(f.HomeIntermediate(f.home,j));assert(core::Restore(f,j));}
    {auto f=State(j);f.home={true,Records({Record(100,0x80),Record(101,0x81)})};assert(!f.HomeIntermediate(f.home,j));}
    {auto f=State(j);f.home={true,Records({Record(900,0x82),Record(101,0x81,0),Record(101,0x81)})};assert(!f.HomeIntermediate(f.home,j));}
    {auto f=State(j);f.home={true,Records({Record(900,0x82),Record(101,0x81,0),Record(555,0x80)})};assert(!f.HomeIntermediate(f.home,j));}
    // A nonexistent previous application can be removed again after one
    // record publishes. An unexpected HOME entry is still preserved.
    {auto first=j;first.home_before={};first.home_after={true,Records({Record(100,0x80),Record(101,0x81)})};auto f=State(first);f.home={true,Records({Record(100,0x80)})};assert(f.HomeIntermediate(f.home,first));assert(core::Restore(f,first)&&!f.home.exists);}
    std::cout<<"USB import journal/rollback tests passed\n";
}
