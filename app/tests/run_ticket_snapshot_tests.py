"""Run actual USB scan ticket-snapshot methods against ES fault scenarios."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/usb_import_scan.cpp').read_text()
es_source=(root/'sphaira/source/yati/nx/es.cpp').read_text()
def block(name):
    start=source.index(name); end=source.index('{',start)+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def es_block(name):
    start=es_source.index(name);end=es_source.index('{',start)+1;depth=1
    while depth:
        depth+=(es_source[end]=='{')-(es_source[end]=='}');end+=1
    return es_source[start:end]
fixture=r'''
#include <algorithm>
#include <array>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdint>
using Result=int;using s32=int32_t;using FsRightsId=std::array<unsigned char,16>;
#define R_TRY(x) do{auto rc=(x);if(rc)return rc;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return r;}while(0)
#define MAKERESULT(m,d) 0x1759
namespace nexus{constexpr Result Invalid=0x1759;}
constexpr Result Fault=123;
namespace es{
std::vector<FsRightsId> common,personal;
bool bad_count{},short_list{},duplicate{},zero{},count_changed{},list_error{},count_error{};
unsigned calls{};
bool IsRightsIdValid(const FsRightsId& id){return id!=FsRightsId{};}
Result Count(bool p,s32* out){++calls;if(count_error)return Fault;*out=bad_count?-1:int((p?personal:common).size());if(count_changed&&calls%2==0)++*out;return 0;}
Result CountCommonTicket(s32* out){return Count(false,out);}
Result CountPersonalizedTicket(s32* out){return Count(true,out);}
Result List(bool p,s32* n,FsRightsId* out,s32 capacity){assert(*n==0);if(list_error)return Fault;auto ids=p?personal:common;assert(capacity==int(ids.size()));if(duplicate&&ids.size()>1)ids[1]=ids[0];if(zero&&!ids.empty())ids[0]={};std::copy(ids.begin(),ids.end(),out);*n=int(ids.size())-int(short_list);return 0;}
Result ListCommonTicket(s32* n,FsRightsId* out,s32 capacity){return List(false,n,out,capacity);}
Result ListPersonalizedTicket(s32* n,FsRightsId* out,s32 capacity){return List(true,n,out,capacity);}
ES_READER
}
struct Session{
const char* stage{};std::vector<FsRightsId> tickets;bool cancel{};
Result Check(){return cancel?456:0;}
READ
CHECK
};
FsRightsId Id(unsigned char n){FsRightsId id{};id[0]=n;return id;}
void Reset(){es::common={Id(2),Id(1)};es::personal={Id(3)};es::calls=0;es::bad_count=es::short_list=es::duplicate=es::zero=es::count_changed=es::list_error=es::count_error=false;}
int main(){
Reset();Session s;assert(!s.ReadTicketSnapshot(s.tickets)&&s.tickets.size()==3);assert(!s.CheckTicketsUnchanged());
std::reverse(es::common.begin(),es::common.end());assert(!s.CheckTicketsUnchanged());
es::personal[0]=Id(4);assert(s.CheckTicketsUnchanged()); // same-count replacement
for(auto flag:{&es::bad_count,&es::short_list,&es::duplicate,&es::zero,&es::count_changed,&es::list_error,&es::count_error}){
Reset();*flag=true;std::vector<FsRightsId> out;assert(s.ReadTicketSnapshot(out));
}
Reset();s.cancel=true;assert(s.CheckTicketsUnchanged()==456);s.cancel=false;
Reset();es::common.clear();es::personal.clear();assert(!s.ReadTicketSnapshot(s.tickets)&&s.tickets.empty());assert(!s.CheckTicketsUnchanged());
Reset();es::common.clear();assert(!s.ReadTicketSnapshot(s.tickets)&&s.tickets==es::personal); // personalised-only is present
Reset();std::vector<FsRightsId> list;assert(!es::GetCommonTickets(list)&&list.size()==2);assert(!es::GetPersonalisedTickets(list)&&list==es::personal);
for(auto flag:{&es::bad_count,&es::short_list,&es::duplicate,&es::zero,&es::count_changed,&es::list_error,&es::count_error}){
Reset();*flag=true;list={Id(99)};assert(es::GetCommonTickets(list)&&list.empty());
}
Reset();es::common.clear();es::personal.clear();assert(!es::GetCommonTickets(list)&&list.empty());assert(!es::GetPersonalisedTickets(list)&&list.empty());
puts("Current-system ticket snapshots: common/personal, empty, ordering, stale lists, partial replies, zero/duplicate IDs, service failures and cancellation passed");
}
'''.replace('\nREAD\n','\n'+block('    Result ReadTicketSnapshot(')+'\n').replace('\nCHECK\n','\n'+block('    Result CheckTicketsUnchanged(')+'\n').replace('ES_READER','\n'.join(es_block(name) for name in ('static Result ReadInstalledTickets(', 'Result GetCommonTickets(', 'Result GetPersonalisedTickets(')))
with tempfile.TemporaryDirectory(prefix='nexus-ticket-snapshot-') as tmp:
    tmp=Path(tmp);cpp=tmp/'test.cpp';cpp.write_text(fixture)
    for opt in ('-O0','-O2'):
        exe=tmp/('test'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
