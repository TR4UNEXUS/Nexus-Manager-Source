#!/usr/bin/env python3
"""Fault injection for the production move publication boundary and UI guidance."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
code=r'''
#include <cassert>
#include <cstdio>
#include "nexus/copy_publication.hpp"
#include "nexus/error_presentation.hpp"
using namespace nexus;
int main(){
 static_assert(registration::FirmwareUpdateRequired!=(505u|(901u<<9)));
 static_assert(registration::MetadataConflict!=(505u|(902u<<9)));
 assert(presentation::ContentReason(registration::FirmwareUpdateRequired));
 assert(presentation::ContentReason(registration::MetadataConflict));
 assert(!presentation::ContentReason(505u|(901u<<9))); // not a firmware error
 assert(!presentation::ContentReason(505u|(902u<<9))); // not a metadata conflict
 assert(presentation::ContentReason(0xA79FD602));
 for(unsigned registered: {0u,registration::ContentAlreadyExists,0x3228cu})
 for(unsigned verified: {0u,0x1759u,0x3228cu})
 for(unsigned removed: {0u,0xe02u}){
   bool owned=true;int publish=0,verify=0,cleanup=0;
   auto rc=FinishCopiedNca(owned,[&]{++publish;return registered;},[&]{++verify;return verified;},[&]{++cleanup;return removed;});
   assert(publish==1);
   if(registered && registered!=registration::ContentAlreadyExists){assert(rc==registered&&owned&&!verify&&!cleanup);continue;}
   assert(verify==1);
   if(verified){assert(rc==verified&&!cleanup&&owned==(registered!=0));continue;}
   if(!registered){assert(!rc&&!owned&&!cleanup);continue;}
   assert(cleanup==1&&rc==removed&&owned==(removed!=0));
 }
 registration::Capabilities old{registration::CapabilityMagic,1,512,8192,3,0};
 assert(!registration::Compatible(old));
 registration::Capabilities current{registration::CapabilityMagic,1,4096,262144,7,0};
 assert(registration::Compatible(current));
 assert(registration::ExplicitInstallTargetSupported(current));
 current.features=15;assert(registration::Compatible(current));
 for(auto result:{registration::SourceIncomplete,registration::SourceNotFound,registration::SourceRecordUnsupported,0x615u})assert(presentation::ContentReason(result));
 current.reserved=1;assert(!registration::Compatible(current));
 puts("PASS: verified collision reuse; no overwrite/replay; I/O, hash, cleanup and compatibility failures stay closed.");
}
'''.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='nexus-report-fixes-') as temp:
 T=Path(temp);(T/'test.cpp').write_text(code)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(T/'test.cpp'),'-o',str(T/'test')],check=True)
  subprocess.run([str(T/'test')],check=True)
