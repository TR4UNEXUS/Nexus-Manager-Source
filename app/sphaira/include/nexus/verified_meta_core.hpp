// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_import_scan_core.hpp"
namespace nexus::verified_meta {
// A modified CNMT NCA can retain its old filename. Validate its complete
// filesystem hash tree and compare its references with independently verified
// installed content before considering it for repair. This is not a signature
// or original-file authenticity claim; no file, key or ticket is changed.
using namespace usb_import;
template<class Hash>
bool Decode(std::span<const std::uint8_t> header,std::span<const std::uint8_t> body,
            std::size_t index,std::uint64_t file_size,Hash hash,Meta& out){
 out={};
 if(header.size()!=0xc00 || index>=4 || file_size<0xc00 || file_size>MaxMetaBytes ||
    Read<std::uint32_t>(header,0x200)!=0x3341434e || header[0x205]!=1 ||
    Read<std::uint64_t>(header,0x208)!=file_size)return false;
 if(std::any_of(header.begin()+0x230,header.begin()+0x240,[](auto b){return b!=0;}))return false;
 unsigned sections{};
 for(unsigned i=0;i<4;++i)if(Read<std::uint32_t>(header,0x240+i*16)||Read<std::uint32_t>(header,0x244+i*16))++sections;
 if(sections!=1)return false;
 const std::uint64_t start=std::uint64_t(Read<std::uint32_t>(header,0x240+index*16))*512;
 const std::uint64_t end=std::uint64_t(Read<std::uint32_t>(header,0x244+index*16))*512;
 if(start<0xc00 || end<=start || end>file_size || body.size()!=end-start)return false;
 const auto fs=header.subspan(0x400+index*512,512);
 if(Read<std::uint16_t>(fs,0)!=2 || fs[2]!=1 || fs[3]!=2 || fs[5]!=0 ||
    (fs[4]!=1 && fs[4]!=3))return false;
 const auto fh=hash(fs);
 if(!std::equal(fh.begin(),fh.end(),header.begin()+0x280+index*32))return false;
 const auto block=Read<std::uint32_t>(fs,0x28),levels=Read<std::uint32_t>(fs,0x2c);
 const auto ho=Read<std::uint64_t>(fs,0x30),hs=Read<std::uint64_t>(fs,0x38);
 const auto po=Read<std::uint64_t>(fs,0x40),ps=Read<std::uint64_t>(fs,0x48);
 if(levels!=2 || block<32 || block>MaxMetaBytes || (block&(block-1)) || !hs || !ps ||
    !Range(ho,hs,body.size()) || !Range(po,ps,body.size()) || !(ho+hs<=po || po+ps<=ho) ||
    hs%32 || (ps+block-1)/block>hs/32)return false;
 const auto table=body.subspan(ho,hs),payload=body.subspan(po,ps);const auto root=hash(table);
 if(!std::equal(root.begin(),root.end(),fs.begin()+8))return false;
 for(std::uint64_t off=0,i=0;off<ps;off+=block,++i){
  const auto digest=hash(payload.subspan(off,std::min<std::uint64_t>(block,ps-off)));
  if(!std::equal(digest.begin(),digest.end(),table.begin()+i*32))return false;
 }
 std::uint64_t off{},size{};
 if(!FindCnmt(payload,payload.size(),off,size))return false;
 std::string reason;
 // A converted NCA can keep its original content ID while CNMT carries the
 // complete converted-file digest. Matches below checks that complete digest.
 return ParseCnmt(payload.subspan(off,size),out,reason,false,true) && out.id==Read<std::uint64_t>(header,0x210);
}
inline bool Matches(const Meta& meta,std::uint64_t id,std::uint32_t version,std::uint8_t type,
                    std::span<const std::uint8_t> installed,std::span<const Content> verified){
 if(meta.id!=id || meta.version!=version || meta.type!=type || installed.size()<8)return false;
 const auto ext=Read<std::uint16_t>(installed,0),count=Read<std::uint16_t>(installed,2);
 if(ext!=meta.extended.size() || !Range(8,ext,installed.size()) ||
    !std::equal(meta.extended.begin(),meta.extended.end(),installed.begin()+8) ||
    !Range(8+ext,std::uint64_t(count)*24,installed.size()))return false;
 // Attribute and metadata-reference count are meaningful; storage is local.
 if(Read<std::uint16_t>(installed,4)!=Read<std::uint16_t>(meta.header,4) || installed[6]!=meta.header[6])return false;
 const auto tail=installed.subspan(8+ext+std::uint64_t(count)*24);
 if(tail.size()!=meta.tail.size() || !std::equal(tail.begin(),tail.end(),meta.tail.begin()))return false;
 std::size_t used{},self{};std::set<std::array<std::uint8_t,16>> seen;
 for(unsigned i=0;i<count;++i){
  const auto row=installed.subspan(8+ext+i*24,24);std::array<std::uint8_t,16> cid{};std::copy_n(row.begin(),16,cid.begin());
  if(!seen.insert(cid).second)return false;
  if(row[22]==0){++self;continue;}
  const auto entry=std::find_if(meta.content.begin(),meta.content.end(),[&](const auto& c){return c.id==cid;});
  if(entry==meta.content.end())return false;
  const auto actual=std::find_if(verified.begin(),verified.end(),[&](const auto& c){return c.id==cid&&c.hash==entry->hash;});
  if(entry==meta.content.end() || actual==verified.end() || entry->type==6 ||
     entry->size!=(Read<std::uint32_t>(row,16)|(std::uint64_t(row[20])<<32)) ||
     entry->attributes!=row[21] || entry->type!=row[22] || entry->id_offset!=row[23] ||
     actual->hash!=entry->hash || actual->size!=entry->size || actual->type!=entry->type || actual->id_offset!=entry->id_offset)return false;
  ++used;
 }
 return self==1 && used && used==static_cast<std::size_t>(std::count_if(meta.content.begin(),meta.content.end(),[](const auto& c){return c.type!=6;}));
}
}
