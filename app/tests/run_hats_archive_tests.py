#!/usr/bin/env python3
"""Compile the production ZIP gate against host minizip, including real packs."""
from pathlib import Path
import subprocess,tempfile,zipfile,sys
A=Path(__file__).resolve().parents[1]
t=(A/'sphaira/source/nexus/hats.cpp').read_text();body=t[t.index('Result ValidateArchive('):t.index('\nResult Extract(')]
fixture=r'''
#include "nexus/hats_core.hpp"
#include <minizip/unzip.h>
#include <minizip/ioapi.h>
#include <set>
#include <cstring>
#include <cassert>
#include <cstdio>
using u64=uint64_t;using Result=unsigned;constexpr Result Bad=2;
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
template<class F>struct Scope{F f;~Scope(){f();}};
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define ON_SCOPE_EXIT(...) Scope CAT(scope_,__COUNTER__){[&]{__VA_ARGS__;}}
namespace sphaira::mz{void FileFuncStdio(zlib_filefunc64_def* f){fill_fopen64_filefunc(f);}}
using namespace nexus::hats;const char* Zip;
'''
with tempfile.TemporaryDirectory(prefix='nexus-hats-zip-') as d:
 p=Path(d);(p/'test.cpp').write_text(fixture+body+'\nint main(int argc,char** argv){assert(argc==3);Zip=argv[1];u64 total{};const auto rc=ValidateArchive(total);assert((rc==0)==(std::string(argv[2])=="good"));}\n')
 subprocess.run(['g++','-std=c++23','-O2','-UNDEBUG','-I'+str(A/'sphaira/include'),'-I'+str(A/'vendor/yyjson/src'),str(p/'test.cpp'),'-lminizip','-o',str(p/'test')],check=True)
 base={'atmosphere/package3':b'0'*2048,'atmosphere/kips/nexus-loader.kip':b'1'*2048,'bootloader/hekate_ipl.ini':b'[config]'}
 cases=[('good',{}),('bad',{'../owned':b'1'}),('bad',{'/owned':b'1'}),('bad',{'ATMOSPHERE/PACKAGE3':b'1'}),('bad',{'config/nexusmanager/hats.ini':b'1'}),('bad',{'a\\owned':b'1'})]
 for i,(want,extra) in enumerate(cases):
  name=p/f'{i}.zip'
  with zipfile.ZipFile(name,'w') as z:
   for n,b in (base|extra).items():z.writestr(n,b)
  subprocess.run([str(p/'test'),str(name),want],check=True)
 for name in sys.argv[1:]:subprocess.run([str(p/'test'),name,'good'],check=True)
 print(f'PASS: production ZIP gate, {len(cases)} valid/malicious cases and {len(sys.argv)-1} real HATS archives')
