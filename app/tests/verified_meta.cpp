// Synthetic metadata only. No console keys or game content.
#include "nexus/verified_meta_core.hpp"
#include <openssl/sha.h>
#include <cassert>
#include <cstdio>
using namespace nexus::verified_meta;
using Bytes=std::vector<uint8_t>;
auto Hash(std::span<const uint8_t> b){std::array<uint8_t,32> h{};SHA256(b.data(),b.size(),h.data());return h;}
template<class T> void Put(Bytes& b,size_t o,T v){assert(o+sizeof(v)<=b.size());memcpy(b.data()+o,&v,sizeof(v));}
int main(){
 size_t checks{};const uint64_t id=0x0100000001234000;
 for(uint8_t type:{0x80,0x81,0x82,0x84}){
  const uint16_t ext=type==0x81?24:type==0x84?32:16;
  Bytes cnmt(32+ext+56);Put(cnmt,0,id);Put(cnmt,8,uint32_t(65536));cnmt[12]=type;Put(cnmt,14,ext);Put(cnmt,16,uint16_t(1));
  Put(cnmt,type==0x84?40:32,id);
  const auto digest=Hash(Bytes(4096,42));const size_t at=32+ext;
  std::copy(digest.begin(),digest.end(),cnmt.begin()+at);std::copy_n(digest.begin(),16,cnmt.begin()+at+32);
  Put(cnmt,at+48,uint32_t(4096));cnmt[at+54]=1;
  Bytes payload(16+24+10+cnmt.size());memcpy(payload.data(),"PFS0",4);Put(payload,4,uint32_t(1));Put(payload,8,uint32_t(10));
  Put(payload,24,uint64_t(cnmt.size()));memcpy(payload.data()+40,"meta.cnmt",10);std::copy(cnmt.begin(),cnmt.end(),payload.begin()+50);
  Bytes body(512),header(0xc00);std::copy(payload.begin(),payload.end(),body.begin()+32);
  const auto ph=Hash(payload);std::copy(ph.begin(),ph.end(),body.begin());
  Put(header,0x200,uint32_t(0x3341434e));header[0x205]=1;Put(header,0x208,uint64_t(0xe00));Put(header,0x210,id);
  Put(header,0x240,uint32_t(6));Put(header,0x244,uint32_t(7));Put(header,0x400,uint16_t(2));header[0x402]=1;header[0x403]=2;header[0x404]=1;
  const auto root=Hash(std::span(body).first(32));std::copy(root.begin(),root.end(),header.begin()+0x408);
  Put(header,0x428,uint32_t(512));Put(header,0x42c,uint32_t(2));Put(header,0x438,uint64_t(32));Put(header,0x440,uint64_t(32));Put(header,0x448,uint64_t(payload.size()));
  const auto fs=Hash(std::span(header).subspan(0x400,512));std::copy(fs.begin(),fs.end(),header.begin()+0x280);
  Meta meta;assert(Decode(header,body,0,0xe00,Hash,meta));++checks;
  Bytes installed(8+ext+48);Put(installed,0,ext);Put(installed,2,uint16_t(2));
  std::copy(meta.extended.begin(),meta.extended.end(),installed.begin()+8);
  installed[8+ext]=7;Put(installed,8+ext+16,uint32_t(0xe00));
  std::copy_n(digest.begin(),16,installed.begin()+8+ext+24);Put(installed,8+ext+40,uint32_t(4096));installed[8+ext+46]=1;
  auto verified=meta.content;assert(Matches(meta,id,65536,type,installed,verified));++checks;
  auto converted=meta;converted.content[0].hash=Hash(Bytes(4096,43));
  assert(!Matches(converted,id,65536,type,installed,verified));
  auto both=verified;both.push_back(converted.content[0]);assert(Matches(converted,id,65536,type,installed,both));checks+=2;
  for(size_t i=0;i<32+payload.size();++i){auto bad=body;bad[i]^=1;Meta out;assert(!Decode(header,bad,0,0xe00,Hash,out));++checks;}
  for(size_t i=0x400;i<0x600;++i){auto bad=header;bad[i]^=1;Meta out;assert(!Decode(bad,body,0,0xe00,Hash,out));++checks;}
  for(size_t i=0;i<32;++i){auto wrong=verified;wrong[0].hash[i]^=1;assert(!Matches(meta,id,65536,type,installed,wrong));++checks;}
  for(size_t i=8+ext+24;i<installed.size();++i){auto bad=installed;bad[i]^=1;assert(!Matches(meta,id,65536,type,bad,verified));++checks;}
  assert(!Matches(meta,id+1,65536,type,installed,verified));assert(!Matches(meta,id,0,type,installed,verified));
  assert(!Matches(meta,id,65536,type,installed,{}));checks+=3;
  auto bad=header;bad[0x230]=1;assert(!Decode(bad,body,0,0xe00,Hash,meta));++checks;
  for(size_t n=0;n<body.size();++n){assert(!Decode(header,std::span(body).first(n),0,0xe00,Hash,meta));++checks;}
 }
 printf("PASS: %zu modified-metadata integrity/reference rejection checks.\n",checks);
}
