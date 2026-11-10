#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;using s32=std::int32_t;using Result=u32;
#define R_TRY(call) do{const auto result_=(call);if(result_)return result_;}while(0)
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(0)
#define R_SUCCEEDED(result) ((result)==0)
#define R_SUCCEED() return 0
#define ON_SCOPE_EXIT(...) ((void)0)
void log_write(const char*,...){}
namespace nexus {constexpr Result Invalid=123;}
enum {NcmContentMetaType_Unknown=0,NcmContentMetaType_Application=0x80,NcmContentMetaType_Patch=0x81,NcmContentMetaType_AddOnContent=0x82,NcmContentMetaType_DataPatch=0x84,NcmContentInstallType_Full=0,NcmContentType_Meta=0};
struct NcmContentMetaKey{u64 id{};u32 version{};u8 type{};};
std::vector<NcmContentMetaKey> installed;unsigned publication_calls{},fail_publication{};
Result ncmContentMetaDatabaseList(int* db,s32* total,s32* count,NcmContentMetaKey* out,size_t capacity,u8,u64,u64,u64,u8){*total=*db?installed.size():0;*count=std::min<size_t>(capacity,*total);if(*count)std::copy_n(installed.begin(),*count,out);return 0;}
Result ncmContentMetaDatabaseHas(int*,bool* out,const NcmContentMetaKey* key){*out=std::any_of(installed.begin(),installed.end(),[&](const auto& i){return i.id==key->id&&i.version==key->version&&i.type==key->type;});return 0;}
struct SavedBool{bool value{true};bool Get()const{return value;}};
struct SavedFilters{SavedBool m_ticket_only,m_skip_base,m_skip_patch,m_skip_addon,m_skip_data_patch,m_skip_ticket;} saved;
struct App{static SavedFilters* GetApp(){return &saved;}};
namespace ui {struct ProgressBox{};}
namespace source {struct Base{};}
namespace container {struct CollectionEntry{std::string name;u64 id{};u32 version{};u8 kind{};};using Collections=std::vector<CollectionEntry>;}
struct NcaCollection:container::CollectionEntry{u8 type{};};
struct CnmtCollection:NcaCollection{std::vector<NcaCollection> ncas;NcmContentMetaKey key;};
struct TikCollection{};
// OUTCOME_PRODUCTION
struct Config{bool ticket_only{},skip_base{},skip_patch{},skip_addon{},skip_data_patch{},skip_ticket{},skip_if_already_installed{},allow_downgrade{};};
struct ConfigOverride{std::optional<bool> ticket_only,skip_base,skip_patch,skip_addon,skip_data_patch,skip_ticket;InstallOutcome* outcome{};};
u64 ApplicationId(const CnmtCollection&){return 100;}
struct Yati {
    Config config;InstallOutcome* outcome{};int ncm_db[2]{1,0},db{1};
    Yati(ui::ProgressBox*,source::Base*){}
    Result Setup(const ConfigOverride& override){
        outcome=override.outcome;
        // FILTER_SETUP_PRODUCTION
        return 0;
    }
    void Cleanup(const NcaCollection&){}
    Result ParseTicketsIntoCollection(std::vector<TikCollection>&,const container::Collections&,bool){return 0;}
    Result InstallCnmtNca(std::span<TikCollection>,CnmtCollection& cnmt,const container::Collections&){cnmt.key={cnmt.id,cnmt.version,cnmt.kind};return 0;}
    Result InstallNca(std::span<TikCollection>,NcaCollection&){return 0;}
    Result ImportTickets(std::span<TikCollection>){return config.skip_ticket?567:0;}
    Result RemoveInstalledNcas(const CnmtCollection&){return 0;}
    Result RegisterNcasAndPushRecord(const CnmtCollection&,u32){return ++publication_calls==fail_publication?789:0;}
    Result GetLatestVersion(const CnmtCollection&,u32&,bool&);
    Result ShouldSkip(const CnmtCollection&,bool&);
};
// GET_LATEST_PRODUCTION
// SHOULD_SKIP_PRODUCTION
// INSTALL_INTERNAL_PRODUCTION
int main(){
    ui::ProgressBox progress;source::Base source;InstallOutcome outcome;
    ConfigOverride options;options.outcome=&outcome;
    const container::Collections base{{"base.cnmt.nca",100,1,NcmContentMetaType_Application}};
    // Existing callers still inherit saved filtering; NAS explicitly disables
    // the six filters, using the actual Setup assignments and installer loop.
    assert(!InstallInternal(&progress,&source,base,options));assert(!outcome.installed_records&&outcome.skipped_records==1&&!publication_calls);
    options.ticket_only=false;options.skip_base=false;options.skip_patch=false;options.skip_addon=false;options.skip_data_patch=false;options.skip_ticket=false;
    const container::Collections all{{"base.cnmt.nca",100,1,NcmContentMetaType_Application},{"update.cnmt.nca",101,1,NcmContentMetaType_Patch},{"dlc.cnmt.nca",102,1,NcmContentMetaType_AddOnContent},{"dlcpatch.cnmt.nca",103,1,NcmContentMetaType_DataPatch}};
    outcome={};publication_calls=0;assert(!InstallInternal(&progress,&source,all,options));assert(outcome.installed_records==4&&!outcome.skipped_records&&publication_calls==4);
    // An older-update-only NSP must remain a no-op; it is counted as skipped
    // rather than counted as installed or allowed to downgrade the game.
    installed={{101,2,NcmContentMetaType_Patch}};outcome={};publication_calls=0;
    const container::Collections update{all[1]};assert(!InstallInternal(&progress,&source,update,options));assert(!outcome.installed_records&&outcome.skipped_records==1&&!publication_calls);
    // A package can legitimately publish some entries and skip an old update.
    outcome={};publication_calls=0;const container::Collections mixed{all[0],all[1]};assert(!InstallInternal(&progress,&source,mixed,options));assert(outcome.installed_records==1&&outcome.skipped_records==1&&publication_calls==1);
    // A metadata/HOME publication failure never increments installed entries.
    installed.clear();outcome={};publication_calls=0;fail_publication=2;assert(InstallInternal(&progress,&source,all,options)==789);assert(outcome.installed_records==1&&!outcome.skipped_records&&publication_calls==2);
    std::cout<<"Actual installer filter, older-update skip and outcome-count tests passed\n";
}
