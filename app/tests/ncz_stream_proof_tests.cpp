#include <iostream>
using Bytes=std::vector<u8>;
constexpr size_t TestBytes=2*1024*1024+0x4000+123;
template<class T>void Append(Bytes& out,const T& value){const auto* bytes=reinterpret_cast<const u8*>(&value);out.insert(out.end(),bytes,bytes+sizeof(value));}
Bytes Original(const u8* key,bool card=false,bool plaintext=false){
    Bytes bytes(TestBytes);for(size_t i=0xc00;i<bytes.size();++i)bytes[i]=(i*13+i/113)%251;
    nca::Header h{};h.magic=0x3341434e;h.size=bytes.size();h.content_type=plaintext?nca::ContentType_Control:0;
    h.distribution_type=card?nca::DistributionType_GameCard:nca::DistributionType_System;
    if(plaintext)memcpy(bytes.data(),&h,sizeof(h));else crypto::cryptoAes128Xts(&h,bytes.data(),key,0,0x200,sizeof(h),true);
    return bytes;
}
Bytes Compress(const Bytes& raw,bool encrypted,Bytes& expected){
    Bytes out(raw.begin(),raw.begin()+0x4000);ncz::Header header{ncz::SectionMagic,1};Append(out,header);
    ncz::Section section{};section.offset=0x4000;section.size=raw.size()-0x4000;section.crypto_type=encrypted?3:1;
    for(unsigned i=0;i<16;++i){section.key[i]=i+3;section.counter[i]=i+1;}
    Append(out,section);expected=raw;
    if(encrypted){u8 counter[16];memcpy(counter,section.counter,16);crypto::UpdateCtr(counter,section.offset);
        Aes128CtrContext aes;aes128CtrContextCreate(&aes,section.key,counter);aes128CtrCrypt(&aes,expected.data()+0x4000,raw.data()+0x4000,section.size);}
    Bytes compressed(ZSTD_compressBound(section.size));const auto size=ZSTD_compress(compressed.data(),compressed.size(),raw.data()+0x4000,section.size,1);assert(!ZSTD_isError(size));
    out.insert(out.end(),compressed.begin(),compressed.begin()+size);return out;
}
bool Run(const Bytes& source,const Bytes& expected,const u8* key,size_t chunk,bool edit=false,bool alias=false){
    Yati y;memcpy(y.keys.header_key,key,32);y.config.ignore_distribution_bit=!edit;
    ThreadData t;t.entry.size=source.size();t.entry.verified_alias=alias;t.write_size=source.size();t.read_buffer_size=chunk;
    HarnessSource input{source.data(),source.size(),chunk};HarnessDestination destination;y.source=&input;y.destination=&destination;t.yati=&y;
    assert(!y.readFuncInternal(&t));assert(!y.decompressFuncInternal(&t));assert(t.output.size()==expected.size());
    u8 expected_hash[32],output_hash[32],header_hash[32];sha256CalculateHash(expected_hash,expected.data(),expected.size());
    sha256CalculateHash(output_hash,t.output.data(),t.output.size());sha256CalculateHash(header_hash,t.output.data(),sizeof(nca::Header));
    assert(!memcmp(output_hash,t.entry.hash,32));assert(t.entry.usb_written.header_known&&!memcmp(header_hash,t.entry.usb_written.header.data(),32));
    if(edit||alias){assert(memcmp(t.output.data(),source.data(),sizeof(nca::Header)));assert(!memcmp(t.output.data()+sizeof(nca::Header),expected.data()+sizeof(nca::Header),expected.size()-sizeof(nca::Header)));
        nca::Header installed{};crypto::cryptoAes128Xts(t.output.data(),&installed,key,0,0x200,sizeof(installed),false);
        assert(installed.distribution_type==nca::DistributionType_System);
        // Stored-byte verification still accepts the original CNMT hash only
        // via the exact distribution-byte normalization, never body changes.
        if(edit){sphaira::nca::ContentHash stored;stored.Update(t.output.data(),t.output.size(),key);assert(stored.Matches(expected_hash,32));
            auto broken=t.output;broken.back()^=1;sphaira::nca::ContentHash damaged;damaged.Update(broken.data(),broken.size(),key);assert(!damaged.Matches(expected_hash,32));}
    }
    else assert(t.output==expected);
    u8 matched[32];assert(t.source_hash.Matches(expected_hash,16,nullptr,nullptr,matched));
    assert(!memcmp(matched,expected_hash,32));
    return t.source_hash.Matches(expected_hash,32);
}
int main(){
    u8 key[32];for(unsigned i=0;i<32;++i)key[i]=i+11;
    const auto raw=Original(key);Bytes expected;const auto compressed=Compress(raw,false,expected);
    for(const auto chunk:{size_t(65536),size_t(1024*1024),size_t(4*1024*1024)}){assert(Run(raw,raw,key,chunk));assert(Run(compressed,expected,key,chunk));}
    const auto encrypted=Compress(raw,true,expected);assert(Run(encrypted,expected,key,1024*1024));
    const auto card=Original(key,true);assert(Run(card,card,key,1024*1024,true));
    const auto compressed_card=Compress(card,false,expected);assert(Run(compressed_card,expected,key,1024*1024,true));
    const auto alias=Original(key,false,true);assert(Run(alias,alias,key,1024*1024,false,true));
    // Body corruption cannot pass the original complete source digest even
    // though all bytes were reconstructed/written and header proof exists.
    auto damaged=raw;damaged.back()^=1;
    Yati y;memcpy(y.keys.header_key,key,32);ThreadData t;t.entry.size=damaged.size();t.write_size=damaged.size();t.read_buffer_size=1024*1024;
    HarnessSource input{damaged.data(),damaged.size(),1024*1024};HarnessDestination destination;y.source=&input;y.destination=&destination;t.yati=&y;
    assert(!y.readFuncInternal(&t)&&!y.decompressFuncInternal(&t));u8 digest[32];sha256CalculateHash(digest,raw.data(),raw.size());assert(!t.source_hash.Matches(digest,32));
    std::cout<<"Actual NCA/NCZ streaming: raw/compressed/encrypted body reconstruction, all-byte source hash, output-header proof after distribution and plaintext-alias edits, corruption rejection passed\n";
}
