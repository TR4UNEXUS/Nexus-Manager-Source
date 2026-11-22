#!/usr/bin/env python3
"""Exercise the actual install preflight with stale NCM records and physical USB."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
s=(A/'sphaira/source/yati/yati.cpp').read_text()
start=s.index('Result Yati::InstallNcaInternal(')
end=s.index('    if (config.skip_if_already_installed',start)
method=s[start:end]+'    return 0;\n}'
fixture=r'''
#include "nexus/localize.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <cstdio>
using Result=unsigned;using u64=std::uint64_t;
#define R_SUCCEEDED(x) ((x)==0)
#define R_TRY(x) do{auto r=(x);if(r)return r;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return(r);}while(0)
#define MAKERESULT(m,d) ((m)|((d)<<9))
struct NcmContentId{unsigned value;};struct TikCollection{};
namespace nca{enum{DistributionType_GameCard=1};struct Header{u64 size{4096};int distribution_type{};};Result DecryptHeader(Header*,int,Header&){return 0;}}
struct NcaCollection{int usb_written{};NcmContentId content_id{123};nca::Header header;bool skipped{};};
bool use_usb=true,stream=false;Result check_rc{},stat_rc=MAKERESULT(140,205),verify_rc{},ticket_rc{},read_rc{};
int checks{},stats{},paths{},verifications{},tickets{},path_queries{};bool directory{};u64 file_size=4096;
struct Destination{bool IsUsb(){return use_usb;}}dest;
struct Source{bool IsStream(){return stream;}}src;
namespace nexus{
constexpr Result Invalid=0x1759;
struct Info{u64 size;bool directory;};
struct Client{Result Check(u64 gen){assert(gen==45);++checks;return check_rc;}Result Stat(u64 gen,const std::string& p,Info& i){assert(gen==45&&p=="/registered/000000AA/physical.nca");++stats;i={file_size,directory};return stat_rc;}};
void Log(const char*,Result){}
bool ContentPath(const char*,bool,std::string&){return false;}
}
namespace nexusapp{
std::string CanonicalUsbContentPath(const NcmContentId& id){assert(id.value==123);++paths;return "/registered/000000AA/physical.nca";}
Result ReadContent(int,int,const NcmContentId&,u64,u64,void*,std::size_t){return read_rc;}
}
// Reproduce the firmware's response for a registered ID whose file is gone.
constexpr int FS_MAX_PATH=0x301;
Result ncmContentStorageGetPath(int*,char*,std::size_t,const NcmContentId*){++path_queries;return 0xA05;}
struct Diagnostic{std::string text;}diagnostics;
struct Yati{
 Destination* destination=&dest;Source* source=&src;u64 usb_generation=45;int cs{},physical_target{},keys{};Diagnostic* diagnostic=&diagnostics;
 Result VerifyReusedNca(const NcaCollection&){++verifications;return verify_rc;}
 Result HasRequiredTicket(const nca::Header&,std::span<TikCollection>){++tickets;return ticket_rc;}
 Result InstallNcaInternal(std::span<TikCollection>,NcaCollection&);
};
METHOD
void reset(){checks=stats=paths=verifications=tickets=path_queries=0;use_usb=true;stream=false;check_rc=verify_rc=ticket_rc=read_rc=0;stat_rc=MAKERESULT(140,205);file_size=4096;directory=false;}
int main(){Yati y;
 reset();NcaCollection missing;assert(y.InstallNcaInternal({},missing)==0);assert(!missing.skipped&&paths==1&&stats==1&&path_queries==0);
 reset();stat_rc=0;NcaCollection existing;assert(y.InstallNcaInternal({},existing)==0);assert(existing.skipped&&verifications==1&&tickets==1&&path_queries==0);
 for(auto failure:{0x8cu,0xA05u,static_cast<unsigned>(MAKERESULT(140,208))}){reset();stat_rc=failure;NcaCollection c;assert(y.InstallNcaInternal({},c)==failure);assert(!c.skipped&&verifications==0);}
 reset();check_rc=0x8c;NcaCollection detached;assert(y.InstallNcaInternal({},detached)==0x8c&&stats==0);
 reset();stat_rc=0;verify_rc=123;NcaCollection corrupt;assert(y.InstallNcaInternal({},corrupt)==123&&!corrupt.skipped);
 reset();stat_rc=0;directory=true;NcaCollection folder;assert(y.InstallNcaInternal({},folder)==nexus::Invalid&&!folder.skipped);
 reset();stat_rc=0;file_size=1;NcaCollection short_file;assert(y.InstallNcaInternal({},short_file)==nexus::Invalid&&!short_file.skipped);
 reset();stat_rc=0;ticket_rc=321;NcaCollection no_ticket;assert(y.InstallNcaInternal({},no_ticket)==321&&!no_ticket.skipped);
 reset();use_usb=false;NcaCollection sd;assert(y.InstallNcaInternal({},sd)==0&&stats==0);
 reset();stream=true;NcaCollection streaming;assert(y.InstallNcaInternal({},streaming)==0&&stats==0);
 puts("USB reinstall preflight passed: stale records, missing/present files, corruption, tickets, disconnects, SD and streaming paths.");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-reinstall-') as tmp:
 t=Path(tmp);(t/'test.cpp').write_text(fixture.replace('METHOD',method))
 subprocess.run(['c++','-std=c++23','-O2','-I'+str(A/'sphaira/include'),str(t/'test.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
