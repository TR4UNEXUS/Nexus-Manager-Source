// Compile the actual production NCZ read/decompress methods against bounded
// in-memory queues. Scheduling/platform calls are mocked, reconstruction isn't.
#include "yati/nx/content_hash.hpp"
#include "nexus/content_compat_core.hpp"
#include "nexus/install_diagnostics.hpp"
#include "nexus/usb_verification_proof.hpp"
#include <vector>
#include <string>
#include <span>
#include <atomic>
#include <algorithm>
#include <bit>
#include <zstd.h>
using Result=unsigned;
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
#define R_TRY(c) do{auto r=(c);if(r)return r;}while(0)
#define R_SUCCEED() return 0
#define R_SUCCEEDED(r) (!(r))
#define ON_SCOPE_EXIT(x)
constexpr Result Result_YatiInvalidNczSectionCount=81,Result_YatiInvalidNczBlockTotal=82,Result_YatiNczSectionNotFound=83,
    Result_YatiInvalidNczZstdError=84,Result_YatiInvalidNcaSha256=85,Result_YatiNczBlockNotFound=86,Result_YatiInvalidNcaReadSize=87;
constexpr size_t INFLATE_BUFFER_MAX=1024*1024*4;
template<class... T> void log_write(const char*,T...){}
namespace crypto=sphaira::crypto;
struct FsRightsId{u8 c[16];};
namespace keys {struct Keys{u8 header_key[32];};struct KeyEntry{u8 data[16];};}
namespace nca {
constexpr int EncryptionType_AesCtr=3,ContentType_Control=2,DistributionType_System=0,DistributionType_GameCard=1;
struct Header{u8 signature[512];u32 magic;u8 distribution_type,content_type,keygen_old,kaek_index;u64 size,program_id;
    u8 pad218[8],keygen,pad221[15];FsRightsId rights_id;u8 tables[0xc0],key_area[4][16],rest[0x8c0];
    u8 GetKeyGeneration()const{return keygen;}};
static_assert(sizeof(Header)==0xc00);
Result DecryptHeader(const void* p,const keys::Keys& k,Header& h){crypto::cryptoAes128Xts(p,&h,k.header_key,0,512,sizeof(h),false);return h.magic==0x3341434e?0:80;}
Result VerifyFixedKey(const Header&){return 0;}
Result EncryptKeak(const keys::Keys&,Header&,u8){return 0;}
Result DecryptKeak(const keys::Keys&,Header&){return 0;}
}
struct Ticket{std::vector<u8> ticket;bool required;};
Ticket* GetTicketCollection(const nca::Header&,std::vector<Ticket>&){return nullptr;}
Result HasRequiredTicket(const nca::Header&,Ticket*){return 0;}
namespace es {Result GetTitleKeyDecrypted(const std::vector<u8>&,const FsRightsId&,u8,const keys::Keys&,keys::KeyEntry&){return 0;}}
namespace ncz {
constexpr u64 SectionMagic=0x4e544345535a434eULL,BlockMagic=0x4b434f4c425a434eULL;
struct Header{u64 magic,total_sections;};
struct Section{u64 offset,size,crypto_type,padding;u8 key[16],counter[16];bool InRange(u64 n)const{return n>=offset && n-offset<size;}};
struct BlockHeader{u64 magic;u8 version,type,padding,block_size_exponent;u32 total_blocks;u64 decompressed_size;
    Result IsValid()const{return magic==BlockMagic && version==2 && type==1 && block_size_exponent>=14 && block_size_exponent<=32 && total_blocks?0:82;}};
struct Block{u32 size;};
struct BlockInfo{u64 offset,size;bool InRange(u64 n)const{return n>=offset && n-offset<size;}};
}
#define NCZ_SECTION_OFFSET (0x4000+sizeof(ncz::Header))
#define NCZ_SECTION_MAGIC ncz::SectionMagic
#define NCZ_BLOCK_MAGIC ncz::BlockMagic
struct Collection{std::string name;s64 size{},offset{};nca::Header header{};int placeholder_id{};bool has_expected_hash{},verified_alias{},modified{};u64 expected_size{};u8 hash[32];nexus::install::HeaderSizeIssue header_size_issue{};nexus::verification::Written usb_written;};
Result ncmContentStorageSetPlaceHolderSize(int*,int*,s64){return 0;}
struct HarnessSource {
    const u8* data{};size_t bytes{},chunk{65536};s64 cursor{};
    Result Read(void* out,s64 off,s64 size,u64* got){
        if(off!=cursor || off<0 || size<0 || u64(off)>bytes || u64(size)>bytes-u64(off))return 88;
        *got=std::min<u64>(size,chunk);memcpy(out,data+off,*got);cursor+=*got;return 0;
    }
};
struct HarnessDestination {Result Validate(){return 0;}};
struct Yati;
struct ThreadData{
    struct Buffer{s64 offset;std::vector<u8> bytes;};
    std::vector<u8> source,output;std::vector<Buffer> buffers;size_t consumed{};
    Collection entry;Collection* nca{&entry};std::vector<Ticket> tik;
    Yati* yati{};
    std::atomic<s64> read_offset{},decompress_offset{},write_size{};
    bool read_running=true,decompress_running=true;
    u64 max_buffer_size{4*1024*1024},read_buffer_size{65536};
    ncz::BlockHeader ncz_block_header{};std::vector<ncz::Section> ncz_sections;std::vector<ncz::BlockInfo> ncz_blocks;
    sphaira::nca::ContentHash source_hash;Sha256Context sha256;
    ThreadData(){sha256ContextCreate(&sha256);}
    Result GetResults(){return 0;}
    Result Read(void* out,s64 size,u64* got);
    Result SetDecompressBuf(std::vector<u8>& buf,s64 off,s64 size){buffers.push_back({off,{buf.begin(),buf.begin()+size}});return 0;}
    Result GetDecompressBuf(std::vector<u8>& buf,s64& off){if(consumed==buffers.size()){buf.clear();return 0;}off=buffers[consumed].offset;buf=buffers[consumed++].bytes;return 0;}
    Result SetWriteBuf(std::vector<u8>& buf,s64 size,bool){sha256ContextUpdate(&sha256,buf.data(),size);output.insert(output.end(),buf.begin(),buf.begin()+size);buf.clear();return 0;}
};
struct Yati{
    struct Progress{void ContentFile(const std::string&,const std::string&,const std::string&,u64,u64){}} progress;Progress* pbox{&progress};
    int cs{};keys::Keys keys{};
    HarnessSource* source{};HarnessDestination* destination{};
    struct Config{bool skip_nca_hash_verify=false,skip_rsa_header_fixed_key_verify=true,ignore_distribution_bit=true,convert_to_standard_crypto=false,lower_master_key=false;}config;
    Result readFuncInternal(ThreadData*);Result decompressFuncInternal(ThreadData*);
};
// READ_PRODUCTION
// NCZ_PRODUCTION
extern "C" __declspec(dllexport) int ValidateLayout(const u64* triples,size_t count,u64 declared){
    if(count>nexus::compat::MaxSections)return 0;
    std::vector<ncz::Section> sections(count);
    for(size_t i=0;i<count;++i){sections[i].offset=triples[i*3];sections[i].size=triples[i*3+1];sections[i].crypto_type=triples[i*3+2];}
    return nexus::compat::ValidSections(sections,declared)?1:0;
}
extern "C" __declspec(dllexport) int Reconstruct(const u8* source,size_t size,const u8* key,const u8* expected,size_t read_chunk){
    Yati y;memcpy(y.keys.header_key,key,32);ThreadData t;t.source.assign(source,source+size);t.entry.size=size;t.write_size=size;t.read_buffer_size=read_chunk;
    HarnessSource input{source,size,read_chunk};HarnessDestination dest;y.source=&input;y.destination=&dest;t.yati=&y;
    auto rc=y.readFuncInternal(&t);if(rc)return -int(rc);rc=y.decompressFuncInternal(&t);if(rc)return -int(rc);
    // Prefix equality additionally catches accidental re-encryption of the
    // preserved NCZ prefix. No installer header edits are enabled here.
    if (!t.ncz_sections.empty() && (t.output.size()<0x4000 || memcmp(t.output.data(),source,0x4000))) return -90;
    return t.source_hash.Matches(expected,32) && !memcmp(t.entry.hash,expected,32)?1:0;
}
extern "C" __declspec(dllexport) int ProbeHeader(const u8* source,size_t size,const u8* key,unsigned* issue,size_t* written){
    Yati y;memcpy(y.keys.header_key,key,32);ThreadData t;t.entry.size=size;t.write_size=size;
    HarnessSource input{source,size,64};HarnessDestination dest;y.source=&input;y.destination=&dest;t.yati=&y;
    auto rc=y.readFuncInternal(&t);if(!rc)rc=y.decompressFuncInternal(&t);
    *issue=unsigned(t.entry.header_size_issue);*written=t.output.size();return int(rc);
}
