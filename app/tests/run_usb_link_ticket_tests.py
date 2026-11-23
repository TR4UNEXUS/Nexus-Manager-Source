"""Actual link-only ticket check and physical publication boundary, with ES/USB faults."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
s=(A/'sphaira/source/yati/yati.cpp').read_text()
def block(start):
 a=s.index(start);b=s.index('{',a);level=1;e=b+1
 while level:
  level+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
ticket=block('Result Yati::CheckReuseTicket(')
# The exact finish branch of VerifyReusedNca is exercised after simulated hashing.
a=s.index('    // Preflight may read/stage small metadata')
b=s.index('    R_TRY(pbox->ShouldExitResult());',a)
finish=s[a:b]
fixture=r'''
#include "nexus/localize.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <cstdio>
using Result=unsigned;using s32=int;using u64=std::uint64_t;
#define R_TRY(x) do{auto rc=(x);if(rc)return rc;}while(0)
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
struct FsRightsId{u64 id{};};struct TikCollection{FsRightsId id;};
constexpr Result Result_YatiTicketNotFound=99;
namespace nca {struct Header{FsRightsId rights_id;};}
namespace es {
std::vector<FsRightsId> common,personal;Result error{};bool changed{},short_list{};
bool IsRightsIdValid(FsRightsId r){return r.id!=0;}
Result CountCommonTicket(s32* n){*n=common.size();return error;}
Result CountPersonalizedTicket(s32* n){*n=personal.size();return error;}
Result ListCommonTicket(s32* n,FsRightsId* out,s32 size){*n=short_list?size-1:size;std::copy_n(common.begin(),size,out);if(changed)common.push_back({55});return error;}
Result ListPersonalizedTicket(s32* n,FsRightsId* out,s32 size){*n=short_list?size-1:size;std::copy_n(personal.begin(),size,out);if(changed)personal.push_back({55});return error;}
}
TikCollection* GetTicketCollection(const nca::Header& h,std::span<TikCollection> tickets){for(auto& t:tickets)if(t.id.id==h.rights_id.id)return &t;return nullptr;}
Result HasRequiredTicket(const nca::Header&,std::span<TikCollection>){return 56;}
namespace nexus {
constexpr Result Invalid=123;struct Info{bool directory{};u64 size{4096};};
Result check_error{},stat_error{};Info observed;
struct Client{Result Check(u64 g){assert(g==10);return check_error;}Result Stat(u64 g,const std::string& p,Info& i){assert(g==10&&p=="physical/7");i=observed;return stat_error;}};
}
namespace nexusapp {std::string CanonicalUsbContentPath(int id){assert(id==7);return "physical/7";}}
struct Nca{int content_id{7};};
struct Destination{int ensured{},routed{};Result error{};Result EnsureRegistered(int id,u64 size){assert(id==7&&size==4096);++ensured;return error;}Result Verify(int,int,u64){++routed;return error;}};
struct Yati {
 bool reuse_existing_usb_only{true},defer_reuse_publication{true};u64 usb_generation{10};int cs{};
 struct Diagnostic{std::string text;} diag;Diagnostic* diagnostic=&diag;
 Destination dest;Destination* destination=&dest;
 Result CheckReuseTicket(const nca::Header&,std::span<TikCollection>);
 Result Finish(const Nca& nca,u64 size){ FINISH return 0; }
};
TICKET
int main(){Yati y;std::vector<TikCollection> tickets{{{7}}};
 assert(!y.CheckReuseTicket({{0}},{})); // ticketless XCI/game content
 assert(!y.CheckReuseTicket({{7}},tickets));
 es::common={{7}};assert(!y.CheckReuseTicket({{7}},{}));
 es::common.clear();es::personal={{7}};assert(!y.CheckReuseTicket({{7}},{}));
 es::personal.clear();assert(y.CheckReuseTicket({{7}},{})==Result_YatiTicketNotFound);
 es::error=70;y.diag.text.clear();assert(y.CheckReuseTicket({{7}},{})==70&&y.diag.text.empty());es::error=0;
 es::common={{7}};es::short_list=true;assert(y.CheckReuseTicket({{7}},{})==nexus::Invalid);es::short_list=false;
 es::changed=true;assert(y.CheckReuseTicket({{7}},{})==nexus::Invalid);es::changed=false;
 y.reuse_existing_usb_only=false;assert(y.CheckReuseTicket({{7}},tickets)==56);y.reuse_existing_usb_only=true;
 assert(!y.Finish({},4096)&&!y.dest.ensured&&!y.dest.routed);
 y.defer_reuse_publication=false;assert(!y.Finish({},4096)&&y.dest.ensured==1&&!y.dest.routed);
 for(int failure=0;failure<4;++failure){
  y.dest.ensured=0;nexus::observed={};nexus::check_error=nexus::stat_error=0;
  if(failure==0)nexus::check_error=71;if(failure==1)nexus::stat_error=72;
  if(failure==2)nexus::observed.size=4000;if(failure==3)nexus::observed.directory=true;
  assert(y.Finish({},4096)&&!y.dest.ensured&&!y.dest.routed);
 }
 puts("USB link checks passed: package/system/ticketless rights, ES failure, changed lists, preflight without registration, direct physical linking and media faults.");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-link-tickets-') as d:
 p=Path(d);(p/'actual.cpp').write_text(fixture.replace('TICKET',ticket).replace('FINISH',finish))
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(A/'sphaira/include'),str(p/'actual.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
