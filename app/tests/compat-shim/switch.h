// Host-only SDK crypto adapter. Never included in a Switch build.
#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <openssl/aes.h>
#include <openssl/sha.h>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;
#define __bswap64 __builtin_bswap64
struct Sha256Context { SHA256_CTX ctx; };
inline void sha256ContextCreate(Sha256Context* c){SHA256_Init(&c->ctx);}
inline void sha256ContextUpdate(Sha256Context* c,const void* p,size_t n){SHA256_Update(&c->ctx,p,n);}
inline void sha256ContextGetHash(Sha256Context* c,void* p){auto copy=c->ctx;SHA256_Final(static_cast<u8*>(p),&copy);}
inline void sha256CalculateHash(void* out,const void* data,size_t size){SHA256(static_cast<const u8*>(data),size,static_cast<u8*>(out));}
struct Aes128Context{AES_KEY key;};
inline void aes128ContextCreate(Aes128Context* c,const void* key,bool enc){if(enc)AES_set_encrypt_key(static_cast<const u8*>(key),128,&c->key);else AES_set_decrypt_key(static_cast<const u8*>(key),128,&c->key);}
inline void aes128EncryptBlock(Aes128Context* c,void* out,const void* in){AES_encrypt(static_cast<const u8*>(in),static_cast<u8*>(out),&c->key);}
inline void aes128DecryptBlock(Aes128Context* c,void* out,const void* in){AES_decrypt(static_cast<const u8*>(in),static_cast<u8*>(out),&c->key);}
struct Aes128XtsContext{Aes128Context data,tweak;u8 iv[16]{};};
inline void aes128XtsContextCreate(Aes128XtsContext* c,const void* a,const void* b,bool enc){aes128ContextCreate(&c->data,a,enc);aes128ContextCreate(&c->tweak,b,true);}
inline void aes128XtsContextResetSector(Aes128XtsContext* c,u64 sector,bool big){u8 iv[16]{};for(int i=0;i<8;++i)iv[big?15-i:i]=sector>>(i*8);aes128EncryptBlock(&c->tweak,c->iv,iv);}
inline void HostXts(Aes128XtsContext* c,void* out,const void* in,size_t n,bool enc){
    assert(n%16==0);auto* dst=static_cast<u8*>(out);auto* src=static_cast<const u8*>(in);
    for(size_t off=0;off<n;off+=16){u8 b[16];for(int i=0;i<16;++i)b[i]=src[off+i]^c->iv[i];
        if(enc)aes128EncryptBlock(&c->data,b,b);else aes128DecryptBlock(&c->data,b,b);
        for(int i=0;i<16;++i)dst[off+i]=b[i]^c->iv[i];
        unsigned carry=0;for(int i=0;i<16;++i){unsigned v=(c->iv[i]<<1)|carry;carry=v>>8;c->iv[i]=v;}if(carry)c->iv[0]^=0x87;
    }
}
inline void aes128XtsEncrypt(Aes128XtsContext* c,void* out,const void* in,size_t n){HostXts(c,out,in,n,true);}
inline void aes128XtsDecrypt(Aes128XtsContext* c,void* out,const void* in,size_t n){HostXts(c,out,in,n,false);}
struct Aes128CtrContext{Aes128Context aes;u8 counter[16],stream[16];size_t used=16;};
inline void aes128CtrContextCreate(Aes128CtrContext* c,const void* key,const void* iv){aes128ContextCreate(&c->aes,key,true);memcpy(c->counter,iv,16);c->used=16;}
inline void aes128CtrCrypt(Aes128CtrContext* c,void* output,const void* input,size_t size){
    auto* out=static_cast<u8*>(output);auto* in=static_cast<const u8*>(input);
    for(size_t i=0;i<size;++i){if(c->used==16){aes128EncryptBlock(&c->aes,c->stream,c->counter);for(int j=15;j>=0;--j)if(++c->counter[j])break;c->used=0;}out[i]=in[i]^c->stream[c->used++];}
}
