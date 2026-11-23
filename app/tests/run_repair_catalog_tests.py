#!/usr/bin/env python3
"""Verify the production catalogue parser with the actual RSA verification helper."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
raw=(A/'sphaira/source/yati/nx/nxdumptool_rsa.c').read_text()
start=raw.rindex('static bool rsa2048VerifySha256BasedSignature(')
helper=raw[start:]
prefix='''#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
typedef uint8_t u8;
#define RSA2048_BYTES 256
#define SHA256_HASH_SIZE 32
#define LOG_MSG_ERROR(...) ((void)0)
static void sha256CalculateHash(void*out,const void*data,size_t size){mbedtls_sha256_ret(data,size,out,0);}
'''
suffix='''bool Verify(const void* data,size_t size,const void* signature,const void* modulus){const u8 e[]={1,0,1};return rsa2048VerifySha256BasedSignature(data,size,signature,modulus,e,3,true);}
'''
cases=r'''
#include "nexus/repair_catalog_core.hpp"
#include "nexus/repair_catalog_key.hpp"
#include "nexus/problem_report_core.hpp"
#include <fstream>
#include <cassert>
#include <iostream>
extern "C" bool Verify(const void*,size_t,const void*,const void*);
using namespace nexus::catalog;
bool Read(const Bytes& b,Catalog& c,unsigned version=2028){return Decode(b,version,c,[](auto x,auto s){return Verify(x.data(),x.size(),s.data(),PublicModulus.data());});}
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);Bytes bytes{std::istreambuf_iterator<char>(f),{}};Catalog c;
 assert(Read(bytes,c)&&c.revision==1&&c.rules.size()==8);unsigned checks=1;
 for(size_t i=0;i<bytes.size();++i){auto changed=bytes;changed[i]^=1;assert(!Read(changed,c)&&c.rules.empty());++checks;}
 for(size_t size=0;size<bytes.size();++size){Bytes part(bytes.begin(),bytes.begin()+size);assert(!Read(part,c));++checks;}
 assert(!Read(bytes,c,2027));assert(Read(bytes,c));
 Evidence e{Condition::MissingLinks,0x0100123400000000ULL,0,0x80};auto rows=Match(c,e,2028);assert(rows.size()==1&&c.rules[rows[0]].action==Action::RepairLinks);
 e.condition=Condition::ConvertedMetadata;e.application=0x0100fa2024c10000ULL;e.content=c.rules[6].content;e.sha256=c.rules[6].sha256;
 assert(Match(c,e,2028).size()==2);e.sha256[0]^=1;assert(Match(c,e,2028).size()==1);e.sha256=c.rules[6].sha256;e.version=1;assert(Match(c,e,2028).size()==1);
 auto next=c;next.revision=2;assert(CanReplace(c,bytes,next,bytes));next.revision=0;assert(!CanReplace(c,bytes,next,bytes));next=c;auto changed=bytes;changed.back()^=1;assert(!CanReplace(c,bytes,next,changed));assert(CanReplace(c,bytes,c,bytes));
 assert(!Allowed(Condition::MissingTicket,Action::RepairLinks)&&!Allowed(Condition(99),Action::RestoreBackup));
 assert(!Text("bad\ntext")&&!Text(std::string("a\0b",3))&&!Text("\xc0\xaf")&&!Text("\xed\xa0\x80")&&Text("Fran\xc3\xa7"));
 using namespace nexus::reports;
 const std::string line="NM2 2026-11-22T01:00:00Z version=0.2.28 session=0123456789abcdef uptime_ms=1 operation=1 task=install-game event=failed rc=0x00000001 value=42\n";
 const auto clean=ManagerLines("password=private\n"+line+"smb://user:private@server/path\n"+line+"NM2 malformed\n");assert(clean==line+line);
 assert(FirmwareLines("build_id=NEXUS-1.12.2-LR1\nusb_live_write_failures=7\nusb_password=private\npath=/secret\nusb_last_result=0x12345678901234567890\n")=="build_id=NEXUS-1.12.2-LR1\nusb_live_write_failures=7\n");
 const auto wake=FirmwareLines("usb_sleep_crypto_refreshes=3\nusb_sleep_last_result=0x0003228C\nusb_sleep_path=/private\nusb_sleep_password=secret\n");
 assert(wake=="usb_wake_sleep_crypto_refreshes=3\nusb_wake_sleep_last_result=0x0003228C\n");
 assert(FirmwareLines(wake)==wake);
 const std::string status="usb_query=0x00000000 mounted=1 ready=1 state=0 usb_result=0x00000000 backup_query=0x00000000 backup_flags=1 backup_result=0x00000000";
 assert(StatusFields(status)&&!StatusFields(status+" private=abc"));
 assert(!Description("password: private")&&!Description("API_KEY=secret")&&!Description(std::string("a\0b",3))&&Description("Game did not start"));
 const auto payload=Payload("0.2.28","repair",1,"",line,"usb_live_read_failures=2\n",status);assert(!payload.empty()&&payload.find("private")==payload.npos&&payload.find("repair")!=payload.npos);
 assert(Payload("0.2.28","unknown",1,"",line,"","").empty());assert(!QueueName("../../private.json")&&QueueName(std::string(64,'a')+".json"));
 std::cout<<"PASS: "<<checks<<" catalogue signature/truncation checks; rule matching, rollback protection, UTF-8 bounds and diagnostic privacy filters.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-catalog-') as tmp:
 T=Path(tmp);(T/'rsa.c').write_text(prefix+helper+suffix);(T/'test.cpp').write_text(cases)
 subprocess.run(['cc','-O2','-c',str(T/'rsa.c'),'-o',str(T/'rsa.o')],check=True)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(T/'test.cpp'),str(T/'rsa.o'),'-lmbedcrypto','-o',str(T/'test')],check=True)
  subprocess.run([str(T/'test'),str(A/'assets/romfs/nexus/repair-catalog.nxrc')],check=True)
