#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess
A=Path(__file__).resolve().parents[1];P=A/'extras/nexus-hats-payload/source'
test=(A/'tests/hats_boot_profile_test.cpp').read_text();pack=test[test.index('std::string Pack('):test.index('std::string Merge(')]
cases=r'''
int main(){
 const std::string ini="bootloader/hekate_ipl.ini",backup="config/nexus-hats-installer/hekate_ipl.ini.bak";
 std::string bmp(40,'\0');bmp[0]='B';bmp[1]='M';
 for(unsigned from=1;from<=3;++from)for(unsigned to=1;to<=3;++to){
  nodes.clear();put(backup,Pack(from));put(ini,Pack(to));
  for(const auto& n:{NEXUS_EMU_ICON,NEXUS_SYS_ICON})put(n,bmp);
  unsigned changed{};assert(restore_hekate_config(true,&changed)==FR_OK&&changed==2&&!nodes.contains(backup));
  const auto result=nodes.at(ini).data;assert(nexus_boot_mode(result.data(),result.size())==(to==1?1:2));
  assert((result.find(NEXUS_EMU_ICON)!=result.npos)==(to==3));
 }
 nodes.clear();put(backup,Pack(2));put(ini,Pack(1));unsigned changed{};
 fail_sync=true;assert(restore_hekate_config(true,&changed)==FR_DISK_ERR&&nodes.at(backup).data==Pack(2));fail_sync=false;
 assert(restore_hekate_config(false,&changed)==FR_OK&&nodes.at(ini).data==Pack(2)&&!nodes.contains(backup));
 put(backup,Pack(2));nodes.erase(ini);assert(restore_hekate_config(true,&changed)==FR_NO_FILE&&nodes.contains(backup));
 put(ini,"corrupt");assert(restore_hekate_config(true,&changed)==FR_INVALID_PARAMETER&&nodes.contains(backup));
 put(ini,Pack(3));assert(restore_hekate_config(true,&changed)==FR_INVALID_PARAMETER&&nodes.contains(backup));
 assert(restore_hekate_config(false,&changed)==FR_OK&&nodes.at(ini).data==Pack(2));
 puts("PASS: production payload boot restore, all editions, read/sync/artwork faults and recovery retention");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-payload-') as d:
 p=Path(d);(p/'test.cpp').write_text('#include "hats_payload_fixture.hpp"\n#include "../extras/shared/nexus_boot_profile.h"\n'+pack+cases)
 flags=['-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'tests/hats_payload_stubs'),'-I'+str(A/'tests')]
 subprocess.run(['cc','-std=gnu11',*flags,'-c',str(P/'boot_config.c'),'-o',str(p/'boot.o')],check=True)
 subprocess.run(['g++','-std=c++23',*flags,str(p/'test.cpp'),str(p/'boot.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
