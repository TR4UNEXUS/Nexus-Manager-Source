"""Read synthetic output from the Windows transfer tests with the Switch decoders."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
fixture=(root/'tests/usb_ticket_store_fixture.hpp').read_text()
begin=fixture.index('struct Sha256Context')
end=fixture.index('namespace sphaira::es',begin)
fixture=fixture[:begin]+'''#include <openssl/sha.h>
#include <fstream>
#include "nexus/pc_removal.hpp"
struct Sha256Context { std::vector<u8> raw; };
void sha256ContextCreate(Sha256Context* c){c->raw.clear();}
void sha256ContextUpdate(Sha256Context* c,const void* p,size_t size){auto* b=static_cast<const u8*>(p);c->raw.insert(c->raw.end(),b,b+size);}
void sha256ContextGetHash(Sha256Context* c,void* out){SHA256(c->raw.data(),c->raw.size(),static_cast<u8*>(out));}
'''+fixture[end:]
fixture=fixture.replace('if(ticket.size()<sizeof(TicketData))return 9;memcpy(out,ticket.data(),sizeof(*out));',
                        'if(ticket.size()!=0x2c0||ticket[0x280]!=2)return 9;memcpy(out,ticket.data()+0x2a0,sizeof(*out));')
header=(root/'sphaira/include/nexus/usb_ticket_store.hpp').read_text()
impl=(root/'sphaira/source/nexus/usb_ticket_store.cpp').read_text()
code=fixture+re.sub(r'^#(?:include|pragma)[^\n]*\n','',header,flags=re.M)+re.sub(r'^#include "[^\n]*\n','',impl,flags=re.M)
code+='''
std::vector<u8> read_file(const char* path){std::ifstream in(path,std::ios::binary);assert(in);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
    assert(argc>=3&&argc<=5);using namespace sphaira::nexusapp;
    auto raw=read_file(argv[1]);assert(raw.size()>48);auto n=raw.size()-32;
    u8 sum[32];SHA256(raw.data(),n,sum);assert(!memcmp(sum,raw.data()+n,32));
    nexus::pc::Receipt r;assert(nexus::pc::Parse(std::span(raw).first(n),r));
    assert(r.game==0x0100000000023000ull && r.members.size()==1);
    const auto m=r.members.front();assert(m.ids.size()==2);
    if(argc==5){
        auto swap=read_file(argv[4]);assert(swap.size()>136);
        const auto body=std::span(swap).first(swap.size()-32);
        const auto hash=[](auto bytes){std::array<u8,32> out{};SHA256(bytes.data(),bytes.size(),out.data());return out;};
        auto digest=hash(body);assert(std::equal(digest.begin(),digest.end(),swap.end()-32));
        nexus::pc::Receipt old,fresh;assert(nexus::pc::ParseReplacement(body,old,fresh,hash));
        assert(old.members.size()==4&&fresh.members.size()==3&&old.game==fresh.game);
        for(auto& m:fresh.members)assert(nexus::pc::Matches(fresh,m.title,m.version,m.type,m.ids));
        swap[11]^=1;assert(!nexus::pc::ParseReplacement(body,old,fresh,hash));swap[11]^=1;
        assert(!nexus::pc::ParseReplacement(body.first(body.size()-1),old,fresh,hash));
        swap[4]=255;swap[5]=255;assert(!nexus::pc::ParseReplacement(body,old,fresh,hash));
    }
    if(argc>=4){
        auto side=read_file(argv[3]);assert(side.size()==88);
        std::array<u8,32> binding{},digest{};std::copy_n(sum,32,binding.begin());
        SHA256(side.data(),side.size()-32,digest.data());std::set<nexus::pc::Id> selected;
        assert(nexus::pc::TicketCandidates(side,binding,digest,{r.game},selected)&&selected.size()==1);
        assert(nexus::pc::TicketCandidates(side,binding,digest,{r.game+1},selected)&&selected.empty());
        binding[1]^=1;assert(!nexus::pc::TicketCandidates(side,binding,digest,{r.game},selected));binding[1]^=1;
        side.back()^=1;assert(!nexus::pc::TicketCandidates(side,binding,digest,{r.game},selected));
    }
    assert(nexus::pc::Matches(r,r.game,0,0x80,m.ids));assert(!nexus::pc::Matches(r,r.game,1,0x80,m.ids));
    assert(!nexus::pc::Parse(std::span(raw).first(n-1),r));
    assert(!nexus::pc::Parse(std::span(raw),r));
    auto pair=read_file(argv[2]);assert(pair.size()>96);FsRightsId rights{};memcpy(&rights,pair.data()+16,16);
    auto path="/nexus/pc-import/v1/tickets/"+Hex(&rights,16)+".bin";files[path]=pair;
    UsbTicketPair original;assert(LoadUsbImportTicketPair(42,rights,original)==0);
    assert(std::equal(original.ticket.begin(),original.ticket.end(),pair.begin()+96));
    assert(std::equal(original.certificate.begin(),original.certificate.end(),pair.begin()+96+original.ticket.size()));
    import_error=99;assert(ImportUsbTicketPair(42,rights)==99);import_error=0;
    files[path].back()^=1;assert(LoadUsbImportTicketPair(42,rights,original)==InvalidUsbTicket);
    std::cout<<"Windows-to-Switch deletion receipt and original ticket bundle verified with SHA-256\\n";
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-wire-') as temp:
    src=Path(temp)/'check.cpp';exe=Path(temp)/'check';src.write_text(code)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(root/'sphaira/include'),str(src),'-lcrypto','-o',str(exe)],check=True)
    subprocess.run([str(exe),*sys.argv[1:]],check=True)

