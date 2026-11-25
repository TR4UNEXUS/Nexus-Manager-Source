#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/usb_ticket_store.hpp"
#include "nexus/hats.hpp"
#include "nexus/nx_Client.hpp"
#include "yati/nx/es.hpp"
#include "defines.hpp"
#include <array>
#include <cstdio>
#include <cstring>

namespace sphaira::nexusapp {
namespace {
constexpr size_t MaxPart=64*1024;
struct Header {
    u32 magic{0x3154584e},version{1},ticket_size{},certificate_size{};
    FsRightsId rights{};
    std::array<u8,32> console{},checksum{};
};
static_assert(sizeof(Header)==96);
std::string Hex(const void* bytes,size_t size){
    static constexpr char alphabet[]="0123456789abcdef";
    const auto* p=static_cast<const u8*>(bytes);std::string out(size*2,'0');
    for(size_t i=0;i<size;++i){out[i*2]=alphabet[p[i]>>4];out[i*2+1]=alphabet[p[i]&15];}
    return out;
}
Result Console(std::array<u8,32>& out){
    R_TRY(splInitialize());ON_SCOPE_EXIT(splExit());
    u64 id{};R_TRY(splGetConfig(SplConfigItem_DeviceId,&id));R_UNLESS(id,InvalidUsbTicket);
    // A stable binding, not encryption. Never write/log the hardware ID.
    constexpr char domain[]="Nexus same-console ticket archive v1";
    Sha256Context sha;sha256ContextCreate(&sha);sha256ContextUpdate(&sha,domain,sizeof(domain));
    sha256ContextUpdate(&sha,&id,sizeof(id));sha256ContextGetHash(&sha,out.data());return 0;
}
std::string Folder(const std::array<u8,32>& console){return "/nexus/tickets/v1/"+Hex(console.data(),console.size());}
std::string Path(const Header& h){return Folder(h.console)+"/"+Hex(&h.rights,sizeof(h.rights))+".bin";}
Result Validate(const FsRightsId& rights,std::span<const u8> ticket,std::span<const u8> cert){
    R_UNLESS(es::IsRightsIdValid(rights) && !ticket.empty() && ticket.size()<=MaxPart && !cert.empty() && cert.size()<=MaxPart,InvalidUsbTicket);
    es::TicketData data{};R_UNLESS(R_SUCCEEDED(es::GetTicketData(ticket,&data)),InvalidUsbTicket);
    R_UNLESS(!memcmp(&rights,&data.rights_id,sizeof(rights)),InvalidUsbTicket);
    // The ES importer performs signature/certificate validation. Discovery
    // only validates framing/identity and never treats a failed import as success.
    return 0;
}
void Checksum(Header& h,std::span<const u8> ticket,std::span<const u8> cert){
    Sha256Context sha;sha256ContextCreate(&sha);
    sha256ContextUpdate(&sha,&h,offsetof(Header,checksum));
    sha256ContextUpdate(&sha,ticket.data(),ticket.size());sha256ContextUpdate(&sha,cert.data(),cert.size());
    sha256ContextGetHash(&sha,h.checksum.data());
}
Result Read(nexus::Client& usb,u64 gen,const std::string& path,std::vector<u8>& out){
    R_TRY(usb.Check(gen));
    nexus::Info info{};R_TRY(usb.Stat(gen,path,info));
    R_UNLESS(!info.directory && info.size>=sizeof(Header) && info.size<=sizeof(Header)+2*MaxPart,InvalidUsbTicket);
    out.resize(info.size);u64 got{};R_TRY(usb.Read(gen,path,0,out.data(),out.size(),got));
    R_UNLESS(got==out.size(),InvalidUsbTicket);return usb.Check(gen);
}
Result Decode(const std::vector<u8>& raw,const Header& wanted,UsbTicketPair& out){
    R_UNLESS(raw.size()>=sizeof(Header),InvalidUsbTicket);Header h;memcpy(&h,raw.data(),sizeof(h));
    R_UNLESS(h.magic==wanted.magic && h.version==wanted.version && h.console==wanted.console &&
        !memcmp(&h.rights,&wanted.rights,sizeof(h.rights)) && h.ticket_size && h.ticket_size<=MaxPart &&
        h.certificate_size && h.certificate_size<=MaxPart && raw.size()==sizeof(h)+h.ticket_size+h.certificate_size,InvalidUsbTicket);
    const std::span<const u8> ticket(raw.data()+sizeof(h),h.ticket_size);
    const std::span<const u8> cert(raw.data()+sizeof(h)+h.ticket_size,h.certificate_size);
    const auto expected=h.checksum;Checksum(h,ticket,cert);R_UNLESS(h.checksum==expected,InvalidUsbTicket);
    R_TRY(Validate(wanted.rights,ticket,cert));out.ticket.assign(ticket.begin(),ticket.end());out.certificate.assign(cert.begin(),cert.end());return 0;
}
Result Directory(nexus::Client& usb,u64 gen,const std::string& path){
    nexus::Info info{};const auto rc=usb.Stat(gen,path,info);
    if(R_SUCCEEDED(rc))return info.directory?0:InvalidUsbTicket;
    R_UNLESS(rc==MissingUsbTicket,rc);R_TRY(usb.CreateDirectory(gen,path));
    R_TRY(usb.Stat(gen,path,info));R_UNLESS(info.directory,InvalidUsbTicket);return 0;
}
}
Result LoadUsbTicketPair(u64 generation,const FsRightsId& rights,UsbTicketPair& out){
    out={};Header wanted;wanted.rights=rights;R_UNLESS(es::IsRightsIdValid(rights),InvalidUsbTicket);
    R_TRY(Console(wanted.console));nexus::Client usb;
    std::vector<u8> raw;R_TRY(Read(usb,generation,Path(wanted),raw));return Decode(raw,wanted,out);
}
Result SaveUsbTicketPair(u64 generation,const FsRightsId& rights,std::span<const u8> ticket,std::span<const u8> certificate){
    R_TRY(Validate(rights,ticket,certificate));Header h;h.rights=rights;h.ticket_size=ticket.size();h.certificate_size=certificate.size();
    R_TRY(Console(h.console));Checksum(h,ticket,certificate);
    nexus::Client usb;const auto path=Path(h);
    std::vector<u8> previous;const auto found=Read(usb,generation,path,previous);
    if(R_SUCCEEDED(found)){
        UsbTicketPair pair;R_TRY(Decode(previous,h,pair));
        // Preserve a valid earlier original; it can differ in signatures or
        // account metadata while still granting exactly the same rights ID.
        return 0;
    }
    R_UNLESS(found==MissingUsbTicket,found);
    R_TRY(Directory(usb,generation,"/nexus"));R_TRY(Directory(usb,generation,"/nexus/tickets"));
    R_TRY(Directory(usb,generation,"/nexus/tickets/v1"));R_TRY(Directory(usb,generation,Folder(h.console)));
    std::vector<u8> raw(sizeof(h)+ticket.size()+certificate.size());memcpy(raw.data(),&h,sizeof(h));
    memcpy(raw.data()+sizeof(h),ticket.data(),ticket.size());memcpy(raw.data()+sizeof(h)+ticket.size(),certificate.data(),certificate.size());
    char suffix[40]{};snprintf(suffix,sizeof(suffix),".%016llx.partial",static_cast<unsigned long long>(armGetSystemTick()));
    const auto stage=path+suffix;
    R_TRY(usb.CreateFile(generation,stage,raw.size()));R_TRY(usb.Write(generation,stage,0,raw.data(),raw.size()));R_TRY(usb.Flush(generation));
    std::vector<u8> check;R_TRY(Read(usb,generation,stage,check));R_UNLESS(check==raw,InvalidUsbTicket);
    // Publish both originals together. A crash cannot expose half a pair, and
    // an existing original is never overwritten. Unfinished .partial files
    // are ignored by discovery and deliberately not removed by recovery.
    R_TRY(usb.RenameNew(generation,stage,path));R_TRY(usb.Flush(generation));
    R_TRY(Read(usb,generation,path,check));R_UNLESS(check==raw,InvalidUsbTicket);return 0;
}
Result LoadUsbImportTicketPair(u64 generation,const FsRightsId& rights,UsbTicketPair& out){
    const Result archived=LoadUsbTicketPair(generation,rights,out);
    if(archived!=MissingUsbTicket)return archived;
    Header expected;
    expected.magic=0x3150434e; // NCP1: original package pair, no console binding
    expected.rights=rights;
    const auto path="/nexus/pc-import/v1/tickets/"+Hex(&rights,sizeof(rights))+".bin";
    nexus::Client usb;
    std::vector<u8> raw;
    R_TRY(Read(usb,generation,path,raw));
    R_UNLESS(::nexus::hats::SupportsPcLibrary(),InvalidUsbTicket);
    return Decode(raw,expected,out);
}
Result ImportUsbTicketPair(u64 generation,const FsRightsId& rights){
    UsbTicketPair pair;R_TRY(LoadUsbImportTicketPair(generation,rights,pair));
    R_TRY(es::Initialize());ON_SCOPE_EXIT(es::Exit());
    nexus::Client usb;R_TRY(usb.Check(generation));
    R_TRY(es::ImportTicket(pair.ticket.data(),pair.ticket.size(),pair.certificate.data(),pair.certificate.size()));
    bool found{};R_TRY(es::IsRightsIdCommon(rights,&found));
    if(!found)R_TRY(es::IsRightsIdPersonalised(rights,&found));
    R_UNLESS(found,InvalidUsbTicket);return usb.Check(generation);
}
}
