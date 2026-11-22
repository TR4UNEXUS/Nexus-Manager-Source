// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/verified_meta_core.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/crypto.hpp"
namespace sphaira::nexusapp {
inline bool DecodeVerifiedMeta(const keys::Keys& keys,std::span<const u8> bytes,nexus::usb_import::Meta& meta){
 if(bytes.size()<sizeof(nca::Header) || bytes.size()>nexus::usb_import::MaxMetaBytes)return false;
 nca::Header header{};
 if(R_FAILED(nca::DecryptHeader(bytes.data(),keys,header)) || header.content_type!=nca::ContentType_Meta ||
    header.kaek_index>=3 || header.GetKeyGeneration()>0x20)return false;
 const auto hash=[](std::span<const u8> data){std::array<u8,32> out{};sha256CalculateHash(out.data(),data.data(),data.size());return out;};
 for(size_t i=0;i<4;++i){
  const auto& section=header.fs_table[i];const auto& fs=header.fs_header[i];
  if(!section.IsValid() || section.GetOffset()<sizeof(header) || section.GetOffsetEnd()<=section.GetOffset() || section.GetOffsetEnd()>bytes.size())continue;
  std::vector<u8> body(bytes.begin()+section.GetOffset(),bytes.begin()+section.GetOffsetEnd());
  if(fs.encryption_type==nca::EncryptionType_AesCtr){
   auto decrypted=header;if(R_FAILED(nca::DecryptKeak(keys,decrypted)))return false;
   u8 ctr[16]{};crypto::SetCtr(ctr,fs.section_ctr,section.GetOffset());Aes128CtrContext ctx;
   aes128CtrContextCreate(&ctx,decrypted.key_area[2].area,ctr);aes128CtrCrypt(&ctx,body.data(),body.data(),body.size());
  }else if(fs.encryption_type!=nca::EncryptionType_None)return false;
  return nexus::verified_meta::Decode({reinterpret_cast<const u8*>(&header),sizeof(header)},body,i,bytes.size(),hash,meta);
 }
 return false;
}
}
