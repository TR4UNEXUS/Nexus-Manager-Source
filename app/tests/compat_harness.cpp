#include "yati/nx/content_hash.hpp"
#include "nexus/content_compat_core.hpp"
#include <vector>
#include <algorithm>
#include <string>
#include <span>

extern "C" __declspec(dllexport) int Verify(const u8* bytes,size_t size,const u8* key,const u8* expected,size_t width,size_t chunk) {
    sphaira::nca::ContentHash hash;
    for(size_t off=0;off<size;){auto n=std::min(size-off,chunk);hash.Update(bytes+off,n,key);off+=n;}
    bool converted{};return hash.Matches(expected,width,&converted)?(converted?2:1):0;
}
extern "C" __declspec(dllexport) int VerifyRecovery(const u8* bytes,size_t size,const u8* key,const u8* expected,size_t width,size_t chunk) {
    sphaira::nca::ContentHash hash;hash.EnablePlaintextOrigin();
    for(size_t off=0;off<size;){auto n=std::min(size-off,chunk);hash.Update(bytes+off,n,key);off+=n;}
    bool converted{},plain{};return hash.Matches(expected,width,&converted,&plain)?(plain?3:converted?2:1):0;
}
extern "C" __declspec(dllexport) int CoreTests() {
    namespace compat=nexus::compat;
    u8 id[16]{};
    assert(compat::ContentName("A1ff6fe672114e96c6056789750f1d0d.nca",id) && id[0]==0xa1);
    assert(compat::ContentName("a1ff6fe672114e96c6056789750f1d0d.cnmt.ncz",id));
    assert(!compat::ContentName("a1ff6fe672114e96c6056789750f1d0d.ncz",id,false));
    assert(!compat::ContentName("prefixa1ff6fe672114e96c6056789750f1d0d.nca",id));
    assert(!compat::ContentName("g1ff6fe672114e96c6056789750f1d0d.nca",id));
    assert(!compat::TableFits(0,64,1024,compat::MaxSections));
    assert(!compat::TableFits(UINT64_MAX,64,UINT64_MAX,compat::MaxSections));
    assert(!compat::TableFits(2,64,127,compat::MaxSections));
    assert(compat::TableFits(47428,64,47428*64,compat::MaxSections));
    struct Section{u64 offset,size,crypto_type;};
    std::vector<Section> sections;
    for(size_t i=0;i<47428;++i)sections.push_back({0x4000+i*16,16,3});
    assert(compat::ValidSections(sections,0x4000+47428*16));
    size_t cursor=0;
    for(size_t i=0;i<sections.size();++i){assert(compat::FindForward(sections,cursor,sections[i].offset)==&sections[i]);assert(cursor==i);}
    assert(!compat::FindForward(sections,cursor,0x4000+47428*16));
    sections[1].offset=0x4008;assert(!compat::ValidSections(sections,0x4000+47428*16));
    sections={{0x5000,0x1000,3}};assert(compat::ValidSections(sections,0x6000));
    cursor=0;assert(!compat::FindForward(sections,cursor,0x4000) && cursor==0);
    sections[0].size=UINT64_MAX;assert(!compat::ValidSections(sections,0x6000));
    sections={{0x4000,0x1000,2}};assert(!compat::ValidSections(sections,0x5000));
    return 1;
}

// The actual Yati::FindContent method is appended by the runner. Only SDK,
// filesystem and progress objects below are mocks; its algorithm is not copied.
using Result=unsigned;
#include "yati/source/read_exact.hpp"
namespace source = sphaira::yati::source;
constexpr Result Result_YatiInvalidNcaReadSize=93;
constexpr Result Result_YatiNcaNotFound=91,Result_YatiInvalidNcaSha256=92;
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
#define R_TRY(c) do{auto r=(c);if(r)return r;}while(0)
#define R_SUCCEED() return 0
#define R_SUCCEEDED(r) (!(r))
constexpr int NcmContentType_Program=1,NcmContentType_Data=2,NcmContentType_Control=3,NcmContentType_HtmlDocument=4,NcmContentType_LegalInformation=5;
struct NcmContentId{u8 data[16];};
struct NcmContentInfo{NcmContentId content_id;u64 size;u8 content_type,id_offset;};
struct NcmPackagedContentInfo{u8 hash[32];NcmContentInfo info;};
void ncmContentInfoSizeToU64(const NcmContentInfo* i,u64* s){*s=i->size;}
template<class... T> void log_write(const char*,T...){}
namespace utils{struct Hex{char str[33]{};};Hex hexIdToStr(const NcmContentId&){return {};}}
namespace container{struct CollectionEntry{std::string name;s64 offset,size;};using Collections=std::vector<CollectionEntry>;}
namespace keys{struct Keys{u8 header_key[32];};}
namespace nca{
constexpr int ContentType_Program=0,ContentType_Control=2,ContentType_Manual=3,ContentType_Data=4,ContentType_PublicData=5;
constexpr int DistributionType_System=0;
struct Header{u8 signatures[0x200];u32 magic;u8 distribution_type,content_type,reserved[2];u64 size,program_id;u8 padding[0xc00-0x218];};static_assert(sizeof(Header)==0xc00);
Result DecryptHeader(const void* in,keys::Keys& k,Header& h){
    sphaira::crypto::cryptoAes128Xts(in,&h,k.header_key,0,512,sizeof(h),false);
    return h.magic==0x3341434e?0:90;
}}
struct Source{const u8* bytes{};size_t size{};int fault{};unsigned Read(void* out,s64 off,s64 count,u64* got){
    if(fault==1)return 87;if(off<0||count<0||u64(off)>size||u64(count)>size-off)return 88;
    *got=fault==2?0:fault==7?std::min<s64>(count,7):count;
    memcpy(out,bytes+off,*got);return 0;}
    bool IsStream(){return fault==3;}};
struct Progress{int fault{};unsigned ShouldExitResult(){return fault==4?89:0;}void NewTransfer(const std::string&){}void UpdateTransfer(u64,u64){}};
struct Destination{unsigned Validate(){return 0;}};
struct Yati{Source* source;Progress* pbox;Destination* destination;keys::Keys keys;u64 alias_bytes_left{256ULL*1024*1024};size_t alias_candidates_left{64};
    Result ReadSourceExact(void*,s64,s64,Result invalid_read=Result_YatiInvalidNcaReadSize);
    Result FindContent(const NcmPackagedContentInfo&,u64,const container::Collections&,const container::CollectionEntry*&);};
// FIND_CONTENT_PRODUCTION
extern "C" __declspec(dllexport) int Alias(const u8* bytes,size_t size,const u8* key,const u8* expected,u64 owner,u64 expected_size,
    const char* names,const u64* offsets,const u64* sizes,size_t count,int fault,int type,int id_offset) {
    Source source{bytes,size,fault};Progress progress{fault};Destination destination;
    Yati yati{&source,&progress,&destination,{}};memcpy(yati.keys.header_key,key,32);
    if(fault==5)yati.alias_bytes_left=0;if(fault==6)yati.alias_candidates_left=0;
    NcmPackagedContentInfo packed{};memcpy(packed.hash,expected,32);memcpy(&packed.info.content_id,expected,16);
    packed.info.size=expected_size;packed.info.content_type=type;packed.info.id_offset=id_offset;
    container::Collections entries;
    for(size_t i=0;i<count;++i){entries.push_back({names,static_cast<s64>(offsets[i]),static_cast<s64>(sizes[i])});names+=strlen(names)+1;}
    const container::CollectionEntry* found{};const auto result=yati.FindContent(packed,owner,entries,found);
    return result?-static_cast<int>(result):1+static_cast<int>(found-entries.data());
}
