"""Compile the actual lazy-registration method with a mocked IPC transport."""
from pathlib import Path
import subprocess
import os

APP=Path(__file__).resolve().parents[1]
source=(APP/'sphaira/source/nexus/nx_Client.cpp').read_text()
method='Result Client::EnsureVirtualSdLoader('+source.split('Result Client::EnsureVirtualSdLoader(',1)[1].split('Result Client::CancelVirtualSd',1)[0]
out=APP/'build/homebrew-tests'
out.mkdir(parents=True,exist_ok=True)
unit=out/'bootstrap_registration_generated.cpp'
unit.write_text(r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "nexus/bootstrap.h"
#include "nexus/vsd_protocol.h"
using u64=uint64_t;using u32=uint32_t;using Result=u32;using Handle=u32;
constexpr Handle INVALID_HANDLE=0;
constexpr Result Unavailable=91;
constexpr int SfBufferAttr_HipcMapAlias=1,SfBufferAttr_In=2;
#define R_FAILED(x) ((x)!=0)
struct Service{};
struct Buffer {const void* ptr;size_t size;};
struct Params {int buffer_attrs[1];Buffer buffers[1];bool in_send_pid;int in_num_handles;Handle in_handles[1];};
static int sent,mode;static u32 flags;static Result query_rc;
static const char* loader=NEXUS_SD_BOOTSTRAP_INFO;
static size_t loader_size=sizeof(NEXUS_SD_BOOTSTRAP_INFO);
static Handle process=42;
const char* envGetLoaderInfo(){return loader;}
size_t envGetLoaderInfoSize(){return loader_size;}
Handle envGetOwnProcessHandle(){return process;}
namespace startup {void Event(const char*,const char*,Result=0,u64=0){}}
struct Client {Service service;Result VirtualSdStatus(NexusVsdStatus& s){s={NEXUS_VSD_MAGIC,1,flags,1,0,0};return query_rc;}Result EnsureVirtualSdLoader(NexusVsdStatus&);};
Result dispatch(Service*,int cmd,const void* in,size_t n,NexusVsdStatus& out,Params p){
    ++sent;assert(cmd==NEXUS_VSD_BEGIN_LOAD && n==8 && *(const u64*)in==0);
    assert(p.in_send_pid && p.in_num_handles==1 && p.in_handles[0]==42);
    assert(p.buffers[0].size==2 && !std::strcmp((const char*)p.buffers[0].ptr,"/"));
    out={NEXUS_VSD_MAGIC,1,NEXUS_VSD_LOADER_SEEN,1,0,0};
    if(mode==1)out.flags|=NEXUS_VSD_ACTIVE;
    if(mode==2)out.magic=0;
    if(mode==3)out.version=9;
    return mode==4?77:0;
}
#define serviceDispatchInOut(s,c,i,o,...) dispatch(s,c,&(i),sizeof(i),o,(Params){__VA_ARGS__})
''' + method + r'''
int main(){
    Client c;NexusVsdStatus s{};
    assert(!c.EnsureVirtualSdLoader(s) && sent==1 && s.flags==1);
    sent=0;flags=1;assert(!c.EnsureVirtualSdLoader(s) && !sent); // old registered HBL still works
    flags=3;assert(c.EnsureVirtualSdLoader(s) && !sent); // never clear someone else's arm
    flags=5;assert(c.EnsureVirtualSdLoader(s) && !sent); // or active session
    flags=0;loader="Unknown";loader_size=8;assert(c.EnsureVirtualSdLoader(s) && !sent);
    loader=NEXUS_SD_BOOTSTRAP_INFO;loader_size=4;assert(c.EnsureVirtualSdLoader(s) && !sent);
    loader_size=sizeof(NEXUS_SD_BOOTSTRAP_INFO);process=0;assert(c.EnsureVirtualSdLoader(s) && !sent);
    process=42;query_rc=123;assert(c.EnsureVirtualSdLoader(s)==123 && !sent);query_rc=0;
    for(mode=1;mode<=4;++mode){sent=0;assert(c.EnsureVirtualSdLoader(s) && sent==1);}
    puts("Production lazy registration: exact loader contract, PID/handle ABI, idle-only, old-loader compatibility, error validation passed");
}
''')
exe=out/'bootstrap_registration_tests.exe'
compiler=os.environ.get('CXX','C:/devkitPro/msys2/usr/bin/g++.exe' if os.name=='nt' else 'g++')
subprocess.run([compiler,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-I'+str(APP/'sphaira/include'),str(unit),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
