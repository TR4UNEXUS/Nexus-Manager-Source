// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/manager_update_core.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

constexpr const char* operator""_nx(const char* text, std::size_t) { return text; }
#include <unistd.h>
namespace update=nexus::manager_update;
namespace manager_update=nexus::manager_update;
using update::Bytes;
using Result=unsigned;
using u8=std::uint8_t;using u64=std::uint64_t;
using NcmStorageId=unsigned;
#define R_FAILED(result) ((result)!=0)
#define R_TRY(expression) do{const Result result=(expression);if(result)return result;}while(false)
#define R_UNLESS(condition,result) do{if(!(condition))return (result);}while(false)

struct Database {
    const Bytes original{1,2,3},replacement{4,5,6,7},foreign{8,9};
    Bytes current=original,backup_before,backup_after;
    unsigned calls{},sets{},commits{},reads{},records{},checks{},backups{};
    std::set<unsigned> failures;
    std::vector<std::string> events;
    bool mutate_failed_set{},external_change{},snapshot_verified{},record_changed{},cancel_after_backup{};
    bool Step(std::string event){events.push_back(std::move(event));return !failures.contains(++calls);}
    bool Read(Bytes& value){++reads;if(!Step("read"))return false;if(external_change&&reads==3)current=foreign;value=current;return true;}
    bool RecordUnchanged(){++records;return Step("record")&&!(record_changed&&records==3);}
    bool BeforeWrite(){++checks;return Step("before write")&&!(cancel_after_backup&&checks==2);}
    bool Backup(const Bytes& before,const Bytes& after){++backups;if(!Step("backup"))return false;assert(current==original);backup_before=before;backup_after=after;snapshot_verified=true;return true;}
    bool Set(const Bytes& next){++sets;assert(snapshot_verified);const bool ok=Step("set");if(ok||mutate_failed_set)current=next;return ok;}
    bool Commit(){++commits;return Step("commit");}
};

// These platform declarations let the harness execute the exact production
// configuration block and control-NCA path selector, not copies of their logic.
constexpr unsigned NcmStorageId_BuiltInUser=4,NcmStorageId_SdCard=5;
namespace nexus{
enum class Target{Internal,Sd,Usb};enum class Provider{Unknown,Absent,Present};
constexpr Result Unavailable=123,Invalid=124;
Provider provider=Provider::Present;Result detect_result{};unsigned detections{};
Provider Detect(Result& result){++detections;result=detect_result;return provider;}
}
nexus::registration::Capabilities reported_caps;
Result capability_error{};unsigned capability_queries{};
Result QueryCapabilities(unsigned command,nexus::registration::Capabilities* out){assert(command==65033&&sizeof(*out)==24);++capability_queries;*out=reported_caps;return capability_error;}
#define serviceDispatch(session,command,...) QueryCapabilities(command,&caps)
// MANAGER_STORAGE_GUARD_PRODUCTION
#undef serviceDispatch
struct ConfigOverride {
    std::optional<nexus::Target> nexus_target;
    std::optional<bool> sd_card_install,ticket_only,skip_base,skip_patch,skip_addon,skip_data_patch,skip_ticket,
        skip_nca_hash_verify,skip_rsa_header_fixed_key_verify,skip_rsa_npdm_fixed_key_verify,
        ignore_distribution_bit,convert_to_common_ticket,convert_to_standard_crypto,lower_master_key,
        lower_system_version,archive_usb_tickets;
};
ConfigOverride Configuration(unsigned storage){
// MANAGER_CONFIGURATION_PRODUCTION
    return config;
}
struct NcmContentStorage{};
struct NcmContentId{unsigned value{};};struct NcmPlaceHolderId{unsigned value{};};
struct NcaCollection{bool skipped{};NcmContentId content_id{44};NcmPlaceHolderId placeholder_id{77};};
namespace fs{struct FsPath{char text[128]{};operator char*(){return text;}};}
unsigned registered_paths{},placeholder_paths{};Result path_error{};
Result ncmContentStorageGetPath(NcmContentStorage*,char* path,std::size_t size,const NcmContentId* id){++registered_paths;assert(size==128&&id->value==44);std::strcpy(path,"registered-control.nca");return path_error;}
Result ncmContentStorageGetPlaceHolderPath(NcmContentStorage*,char* path,std::size_t size,const NcmPlaceHolderId* id){++placeholder_paths;assert(size==128&&id->value==77);std::strcpy(path,"placeholder-control.nca");return path_error;}
// MANAGER_CONTROL_PATH_PRODUCTION

// Exercise the production snapshot writer with real temporary-file I/O. Only
// the Switch SD commit, clock and SHA primitive are platform mocks here.
unsigned sd_commits{};bool sd_available{true},commit_failure{};u64 tick=100;
int* fsdevGetDeviceFileSystem(const char* name){static int device;assert(std::string(name)=="sdmc:");return sd_available?&device:nullptr;}
Result fsFsCommit(int*){++sd_commits;return commit_failure?nexus::Unavailable:0;}
u64 armGetSystemTick(){return tick;}
void sha256CalculateHash(void* output,const void* input,std::size_t size){
    assert(size);auto* out=static_cast<u8*>(output);const auto* in=static_cast<const u8*>(input);
    for(unsigned i=0;i<32;++i)out[i]=in[i%size]^u8(size+i);
}
struct SnapshotWriter{
    update::Identity identity{update::TitleId,0,0x80,0,5};Result error{};std::string backup;
    bool Ok(Result result){if(result&&!error)error=result;return result==0;}
// MANAGER_SNAPSHOT_WRITER_PRODUCTION
};
Bytes ReadFile(const std::string& path){std::ifstream file(path,std::ios::binary);assert(file);return Bytes(std::istreambuf_iterator<char>(file),{});}
u64 LittleEndian(const Bytes& bytes,std::size_t at,unsigned count){u64 result{};for(unsigned i=0;i<count;++i)result|=u64(bytes.at(at+i))<<(i*8);return result;}

std::vector<update::Content> ValidContent(){
    std::vector<update::Content> content(3);
    for(unsigned i=0;i<3;++i){content[i].id[0]=i+1;content[i].owner=update::TitleId;content[i].size=8192;content[i].kind=static_cast<update::Kind>(i);content[i].rights_free=true;}
    return content;
}
int main(){
    // Run the actual public IPC guard with controlled provider/capability
    // results. Old NCM unknown-command errors propagate instead of permitting
    // an update or requiring a USB device to be present.
    reported_caps={nexus::registration::CapabilityMagic,1,nexus::registration::MaxTitles,
        nexus::registration::MaxContent,nexus::registration::FeatureUsbRegistration|nexus::registration::FeatureExplicitInstallTarget,0};
    const auto supported=reported_caps;std::string detail;
    assert(CheckManagerUpdateStorage(NcmStorageId_BuiltInUser,detail)==0&&nexus::detections==0&&capability_queries==0);
    assert(CheckManagerUpdateStorage(0,detail)==nexus::Invalid&&nexus::detections==0);
    nexus::provider=nexus::Provider::Absent;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==0&&capability_queries==0);
    nexus::provider=nexus::Provider::Unknown;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==nexus::Unavailable&&capability_queries==0);
    nexus::detect_result=234;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==234&&capability_queries==0);
    nexus::provider=nexus::Provider::Absent;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==234&&capability_queries==0);
    nexus::provider=nexus::Provider::Present;nexus::detect_result=0;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==0&&capability_queries==1);
    for(unsigned fault=0;fault<8;++fault){reported_caps=supported;
        if(fault==0)reported_caps.features=1;if(fault==1)reported_caps.features=2;
        if(fault==2)reported_caps.magic=0;if(fault==3)reported_caps.abi=2;
        if(fault==4)--reported_caps.titles;if(fault==5)--reported_caps.content;
        if(fault==6)reported_caps.reserved=1;if(fault==7)reported_caps.features=0;
        assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==nexus::registration::FirmwareUpdateRequired);
        assert(detail.find("U1")!=std::string::npos&&detail.find("internal storage")!=std::string::npos);
    }
    reported_caps=supported;capability_error=345;
    assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==345);
    const auto queries=capability_queries;
    assert(CheckManagerUpdateStorage(NcmStorageId_BuiltInUser,detail)==0&&capability_queries==queries);
    capability_error=0;reported_caps.features|=128;assert(CheckManagerUpdateStorage(NcmStorageId_SdCard,detail)==0);
    assert(nexus::registration::Compatible({nexus::registration::CapabilityMagic,1,nexus::registration::MaxTitles,nexus::registration::MaxContent,1,0}));

    const update::Identity installed{update::TitleId,0,0x80,0,5};
    assert(update::SameInstalledApplication(installed,installed,1));
    for(unsigned field=0;field<6;++field){auto incoming=installed;
        if(field==0)++incoming.title;if(field==1)++incoming.version;if(field==2)++incoming.type;
        if(field==3)++incoming.install_type;if(field==4)incoming.storage=4;
        assert(!update::SameInstalledApplication(installed,incoming,field==5?2:1));
    }
    for(unsigned storage=0;storage<10;++storage){auto incoming=installed;incoming.storage=storage;
        assert(update::SameInstalledApplication(incoming,incoming,1)==(storage==4||storage==5));}
    auto wrong_title=installed;++wrong_title.title;assert(!update::SameInstalledApplication(wrong_title,wrong_title,1));
    auto patch=installed;patch.type=0x81;assert(!update::SameInstalledApplication(patch,patch,1));
    auto partial=installed;partial.install_type=1;assert(!update::SameInstalledApplication(partial,partial,1));
    assert(!update::SameInstalledApplication(installed,installed,0));

    const auto layout=ValidContent();assert(update::ValidLayout(layout));
    auto reordered=layout;std::reverse(reordered.begin(),reordered.end());assert(update::ValidLayout(reordered));
    for(unsigned index=0;index<layout.size();++index)for(unsigned field=0;field<9;++field){auto bad=layout;
        if(field==0)++bad[index].owner;if(field==1)bad[index].size=0xbff;if(field==2)bad[index].size=64ULL*1024*1024+1;
        if(field==3)bad[index].offset=1;if(field==4)bad[index].rights_free=false;
        if(field==5)bad[index].kind=update::Kind::Other;if(field==6)bad[index].id={};
        if(field==7)bad[index].id=bad[(index+1)%3].id;if(field==8)bad[index].kind=static_cast<update::Kind>(99);
        assert(!update::ValidLayout(bad));
    }
    auto extra=layout;extra.push_back(layout.front());assert(!update::ValidLayout(extra));
    auto missing=layout;missing.pop_back();assert(!update::ValidLayout(missing));
    auto duplicate=layout;duplicate[2].kind=update::Kind::Program;assert(!update::ValidLayout(duplicate));

    // Decode the actual bounded routing format. A physical duplicate on SD is
    // not enough if this title or its exact NCAs are still routed to USB.
    nexus::repair::Snapshot absent;assert(update::CheckRouting(absent,layout)==update::Routing::Native);
    nexus::repair::Indexes indexes;indexes.titles[123]=0;
    std::array<std::uint8_t,16> unrelated{};unrelated[0]=99;indexes.content.insert(unrelated);
    indexes.mappings[123][0]=unrelated;
    assert(update::CheckRouting(nexus::repair::Encode(indexes),layout)==update::Routing::Native);
    for(unsigned field=0;field<4;++field){auto routed=indexes;
        if(field==0)routed.titles[update::TitleId]=0;
        if(field==1)routed.mappings[update::TitleId]={};
        if(field==2)routed.content.insert(layout[0].id);
        if(field==3)routed.mappings[123][1]=layout[2].id;
        assert(update::CheckRouting(nexus::repair::Encode(routed),layout)==update::Routing::Usb);
    }
    auto malformed=nexus::repair::Encode(indexes);malformed[0].data.pop_back();
    assert(update::CheckRouting(malformed,layout)==update::Routing::Invalid);
    malformed={};malformed[1].exists=true;assert(update::CheckRouting(malformed,layout)==update::Routing::Invalid);

    std::array<char,16> display{};std::strcpy(display.data(),"0.2.4");
    assert(update::DisplayVersionMatches(display,"0.2.4"));assert(!update::DisplayVersionMatches(display,"0.2.3"));
    assert(!update::DisplayVersionMatches(display,"0.2"));assert(!update::DisplayVersionMatches(display,""));
    display[5]='x';assert(!update::DisplayVersionMatches(display,"0.2.4"));
    display.fill('x');assert(!update::DisplayVersionMatches(display,"xxxxxxxxxxxxxxxx"));
    assert(!update::DisplayVersionMatches(std::span(display).first(15),"0.2.4"));
    std::array<std::uint8_t,32> digest{};std::string expected(64,'0');
    assert(update::DigestMatches(digest,expected));digest[31]=0xaf;expected.replace(62,2,"af");assert(update::DigestMatches(digest,expected));
    expected[63]='F';assert(!update::DigestMatches(digest,expected));assert(!update::DigestMatches(digest,"af"));

    Database successful;assert(update::ReplaceMetadata(successful,successful.original,successful.replacement)==update::Publication::Installed);
    assert(successful.current==successful.replacement&&successful.sets==1&&successful.commits==1);
    assert(successful.backup_before==successful.original&&successful.backup_after==successful.replacement);
    const std::vector<std::string> order{"read","record","before write","backup","read","record","before write","set","commit","read","record"};
    assert(successful.events==order);
    // Fail every actual transaction boundary, including errors after a Set may
    // have changed memory. Before publication there are no database writes;
    // after it, the exact previous metadata is restored and verified.
    for(unsigned fail=1;fail<=successful.calls;++fail)for(bool changed:{false,true}){
        Database db;db.failures={fail};db.mutate_failed_set=changed;
        const auto result=update::ReplaceMetadata(db,db.original,db.replacement);
        assert(db.current==db.original);
        if(fail<8){assert(db.sets==0);assert(result==(fail==4?update::Publication::BackupFailed:update::Publication::Unchanged));}
        else {assert(result==update::Publication::Restored&&db.sets==2);assert(db.events.back()=="read");}
    }
    // Rollback failures never report installed/restored. Backups survive every
    // case so callers can report recovery instead of claiming the old app won.
    for(unsigned rollback_fail:{10u,11u,12u,13u}){
        Database db;db.failures={9,rollback_fail};
        assert(update::ReplaceMetadata(db,db.original,db.replacement)==update::Publication::RestoreFailed);
        assert(db.snapshot_verified&&db.backup_before==db.original&&db.backup_after==db.replacement);
    }
    Database concurrent;concurrent.external_change=true;
    assert(update::ReplaceMetadata(concurrent,concurrent.original,concurrent.replacement)==update::Publication::RestoreFailed);
    assert(concurrent.current==concurrent.foreign&&concurrent.sets==1);
    Database ns_change;ns_change.record_changed=true;
    assert(update::ReplaceMetadata(ns_change,ns_change.original,ns_change.replacement)==update::Publication::Restored);
    assert(ns_change.current==ns_change.original);
    Database cancelled;cancelled.cancel_after_backup=true;
    assert(update::ReplaceMetadata(cancelled,cancelled.original,cancelled.replacement)==update::Publication::Unchanged);
    assert(cancelled.sets==0&&cancelled.snapshot_verified);
    Database unexpected;unexpected.current={99};assert(update::ReplaceMetadata(unexpected,unexpected.original,unexpected.replacement)==update::Publication::Unchanged&&unexpected.sets==0);
    for(const Bytes& bad:{Bytes{},Bytes(update::MaxMetadata+1,1)}){Database db;
        assert(update::ReplaceMetadata(db,bad,db.replacement)==update::Publication::Unchanged&&db.calls==0);
        assert(update::ReplaceMetadata(db,db.original,bad)==update::Publication::Unchanged&&db.calls==0);
    }

    for(auto storage:{NcmStorageId_BuiltInUser,NcmStorageId_SdCard}){const auto config=Configuration(storage);
        assert(config.nexus_target==(storage==4?nexus::Target::Internal:nexus::Target::Sd));
        assert(config.sd_card_install==(storage==5));
        for(const auto value:{config.ticket_only,config.skip_base,config.skip_nca_hash_verify,config.convert_to_common_ticket,
            config.convert_to_standard_crypto,config.lower_master_key,config.lower_system_version,config.archive_usb_tickets})assert(value.has_value()&&!*value);
        for(const auto value:{config.skip_patch,config.skip_addon,config.skip_data_patch,config.skip_ticket,config.ignore_distribution_bit,
            config.skip_rsa_header_fixed_key_verify,config.skip_rsa_npdm_fixed_key_verify})assert(value.has_value()&&*value);
    }
    NcmContentStorage storage;NcaCollection control;fs::FsPath path;
    assert(ManagerControlPath(storage,control,path)==0&&placeholder_paths==1&&registered_paths==0);
    assert(std::string(path.text)=="placeholder-control.nca");
    control.skipped=true;assert(ManagerControlPath(storage,control,path)==0&&placeholder_paths==1&&registered_paths==1);
    assert(std::string(path.text)=="registered-control.nca");path_error=123;
    assert(ManagerControlPath(storage,control,path)==123);
    std::filesystem::create_directories("sdmc:/switch/nexusmanager/updates");
    SnapshotWriter writer;const Bytes old_meta{1,2,3,4},new_meta{5,6,7,8,9};
    assert(writer.Backup(old_meta,new_meta)&&sd_commits==1);
    const auto saved=ReadFile(writer.backup);assert(saved.size()==96+old_meta.size()+new_meta.size());
    assert(std::string(saved.begin(),saved.begin()+7)=="NXMUPD1");
    assert(LittleEndian(saved,8,8)==update::TitleId&&LittleEndian(saved,16,4)==0);
    assert(saved[20]==0x80&&saved[21]==0&&saved[22]==5&&saved[23]==0);
    assert(LittleEndian(saved,24,4)==old_meta.size()&&LittleEndian(saved,28,4)==new_meta.size());
    std::array<u8,32> hash{};sha256CalculateHash(hash.data(),old_meta.data(),old_meta.size());assert(std::equal(hash.begin(),hash.end(),saved.begin()+32));
    sha256CalculateHash(hash.data(),new_meta.data(),new_meta.size());assert(std::equal(hash.begin(),hash.end(),saved.begin()+64));
    assert(std::equal(old_meta.begin(),old_meta.end(),saved.begin()+96));
    assert(std::equal(new_meta.begin(),new_meta.end(),saved.begin()+96+old_meta.size()));
    SnapshotWriter collision;assert(!collision.Backup(old_meta,new_meta)&&collision.error==nexus::Unavailable);
    assert(ReadFile(writer.backup)==saved&&sd_commits==1); // Exclusive create never overwrites a backup.
    ++tick;commit_failure=true;SnapshotWriter uncommitted;assert(!uncommitted.Backup(old_meta,new_meta)&&uncommitted.error==nexus::Unavailable);
    assert(std::filesystem::exists(uncommitted.backup));commit_failure=false;
    ++tick;sd_available=false;SnapshotWriter missing_sd;assert(!missing_sd.Backup(old_meta,new_meta)&&missing_sd.error==nexus::Unavailable);
    std::cout<<"Manager self-update: actual identity/layout/version/digest policies, deterministic settings, control reuse and every metadata transaction/rollback boundary passed\n";
}
