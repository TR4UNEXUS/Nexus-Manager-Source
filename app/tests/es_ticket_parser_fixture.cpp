#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;
using Result=u32;
struct FsRightsId { u8 bytes[16]; };
constexpr Result Result_EsBadTicketSize=1, Result_EsBadTitleKeyType=2,
    Result_EsInvalidTicketFromatVersion=3, Result_EsInvalidTicketKeyType=4,
    Result_EsInvalidTicketKeyRevision=5;
#define R_UNLESS(condition, result) do { if(!(condition)) return result; } while(0)
#define R_TRY(expression) do { const auto result=(expression); if(result) return result; } while(0)
#define R_THROW(result) return result
#define R_SUCCEED() return 0
#define log_write(...) do {} while(0)

namespace sphaira::es {
// PRODUCTION_TYPES
Result GetTicketDataOffset(std::span<const u8>,u64&,bool=false);
// PRODUCTION_FUNCTIONS
}

int main(int argc,char**) {
    using namespace sphaira::es;
    if(argc>1) {
        const auto one=std::make_unique<u8[]>(1);
        one[0]=0;
        u64 offset=999;
        return GetTicketDataOffset({one.get(),1},offset)==Result_EsBadTicketSize?0:1;
    }
    for(size_t length=0;length<sizeof(u32);++length) {
        const auto bytes=length?std::make_unique<u8[]>(length):nullptr;
        for(bool certificate:{false,true}) {
            u64 offset=UINT64_MAX;
            assert(GetTicketDataOffset({bytes.get(),length},offset,certificate)==Result_EsBadTicketSize);
            assert(offset==0);
            TicketData out{},before{};
            std::memset(&out,0x5a,sizeof(out));before=out;
            assert(GetTicketData({bytes.get(),length},&out)==Result_EsBadTicketSize);
            assert(!std::memcmp(&out,&before,sizeof(out)));
        }
    }
    const std::array<std::pair<u32,size_t>,7> signatures{{
        {SigType_Rsa4096Sha1,0x240},{SigType_Rsa2048Sha1,0x140},{SigType_Ecc480Sha1,0x80},
        {SigType_Rsa4096Sha256,0x240},{SigType_Rsa2048Sha256,0x140},{SigType_Ecc480Sha256,0x80},
        {SigType_Hmac160Sha1,0x40}}};
    for(const auto& [signature,expected]:signatures) {
        for(bool certificate:{false,true}) {
            std::array<u8,4> bytes{};
            const u32 value=certificate?std::byteswap(signature):signature;
            std::memcpy(bytes.data(),&value,sizeof(value));
            u64 offset=UINT64_MAX;
            assert(GetTicketDataOffset(bytes,offset,certificate)==0);
            assert(offset==expected);
        }
        const size_t full_size=expected+sizeof(TicketData);
        std::vector<u8> bytes(full_size);
        std::memcpy(bytes.data(),&signature,sizeof(signature));
        TicketData out{};
        for(size_t length=4;length<full_size;++length)
            assert(GetTicketData(std::span<const u8>(bytes).first(length),&out)==Result_EsBadTicketSize);
        TicketData expected_data{};
        expected_data.format_version=2;
        expected_data.master_key_revision=0x20;
        expected_data.rights_id.bytes[0]=0x55;
        for(auto type:{TitleKeyType_Common,TitleKeyType_Personalized}) {
            expected_data.title_key_type=type;
            std::memcpy(bytes.data()+expected,&expected_data,sizeof(expected_data));
            assert(GetTicketData(bytes,&out)==0);
            assert(!std::memcmp(&out,&expected_data,sizeof(out)));
        }
        auto invalid=expected_data;invalid.format_version=3;
        std::memcpy(bytes.data()+expected,&invalid,sizeof(invalid));
        assert(GetTicketData(bytes,&out)==Result_EsInvalidTicketFromatVersion);
        invalid=expected_data;invalid.title_key_type=2;
        std::memcpy(bytes.data()+expected,&invalid,sizeof(invalid));
        assert(GetTicketData(bytes,&out)==Result_EsInvalidTicketKeyType);
        invalid=expected_data;invalid.master_key_revision=0x21;
        std::memcpy(bytes.data()+expected,&invalid,sizeof(invalid));
        assert(GetTicketData(bytes,&out)==Result_EsInvalidTicketKeyRevision);
    }
    std::array<u8,4> invalid{0xff,0xff,0xff,0xff};
    u64 offset=999;
    assert(GetTicketDataOffset(invalid,offset)==Result_EsBadTitleKeyType);
    assert(offset==0);
    TicketData out{};
    assert(GetTicketData(invalid,&out)==Result_EsBadTitleKeyType);
    puts("Actual ES parser: short input, all signature offsets, certificate endian, truncated body and ticket fields passed.");
}
