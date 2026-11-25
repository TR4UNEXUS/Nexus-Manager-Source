#!/usr/bin/env python3
"""Exercise the real HATS boot handoff with injected SD/commit/reboot failures."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
source=(A/'sphaira/source/nexus/hats.cpp').read_text()
start=source.index('Result Reboot(std::string& detail)')
end=source.index('\nvoid Display()',start)
body=source[start:end]
fixture=r'''
#include <cassert>
#include <map>
#include <string>
#include <cstdio>
using Result=unsigned;constexpr Result Bad=2,Io=3,Fault=7;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define R_TRY(e) do{auto rc=(e);if(rc)return rc;}while(0)
#define R_UNLESS(c,r) do{if(!(c))return r;}while(0)
template<class F>struct Scope{F f;~Scope(){f();}};
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define ON_SCOPE_EXIT(...) Scope CAT(scope_,__COUNTER__){[&]{__VA_ARGS__;}}
std::string operator""_nx(const char* s,size_t n){return {s,n};}
constexpr const char *Boot="/bootloader/hekate_ipl.ini",*BootBackup="/config/nexus-hats-installer/hekate_ipl.ini.bak",
 *Stage="/config/nexus-hats-installer/hats-staging",*Payload="/switch/nexus-hats-installer/hats-installer.bin",
 *MM_CONTENTS_POLICY_PATH="/config/nexus-hats-installer/policy";
std::map<std::string,std::string> disk;unsigned step{},failure{},writes{};bool persistent{},reboot_fails{};
bool FaultNow(){return ++step==failure||(persistent&&failure&&step>failure);}
namespace fs{struct FsNativeSd{
 Result GetFsOpenResult(){return FaultNow()?Fault:0;}
 bool FileExists(const char* p){return disk.contains(p);}
 Result Commit(){return FaultNow()?Fault:0;}
 Result DeleteFile(const char* p){if(FaultNow())return Fault;disk.erase(p);return 0;}
 Result RenameFile(const char* a,const char* b){if(FaultNow())return Fault;if(disk.contains(b)||!disk.contains(a))return Bad;disk[b]=disk[a];disk.erase(a);return 0;}
};}
std::string ReadText(const char* p){if(FaultNow())return {};return disk.contains(p)?disk[p]:"";}
Result SaveText(fs::FsNativeSd&,const char* p,const std::string& s){++writes;if(FaultNow()){disk[p]=s.substr(0,s.size()/2);return Fault;}disk[p]=s;return 0;}
Result romfsInit(){return FaultNow()?Fault:0;}void romfsExit(){}
Result appletRequestToReboot(){return reboot_fails||FaultNow()?Fault:0;}
Result spsmInitialize(){return reboot_fails||FaultNow()?Fault:0;}Result spsmShutdown(bool){return reboot_fails||FaultNow()?Fault:0;}void spsmExit(){}
Result bpcInitialize(){return reboot_fails||FaultNow()?Fault:0;}Result bpcRebootSystem(){return reboot_fails||FaultNow()?Fault:0;}void bpcExit(){}
void Reset(){disk.clear();step=writes=0;persistent=reboot_fails=false;disk[Boot]="[config]\nautoboot=1\n[CFW]\nkip1=nexus-loader.kip\n";disk[Payload]="payload";disk[MM_CONTENTS_POLICY_PATH]="policy";disk[std::string(Stage)+"/atmosphere/package3"]="firmware";disk["romfs:/nexus-hats/hekate_ipl_mod.ini"]="[HATS Installer]\npayload=hats-installer.bin\n";}
'''
cases=r'''
int main(){
 Reset();const auto original=disk[Boot];std::string detail;assert(!Reboot(detail));const auto steps=step;
 assert(disk[BootBackup]==original&&disk[Boot].find("HATS Installer")!=std::string::npos);
 unsigned checks{};
 for(unsigned at=1;at<=steps+5;++at)for(bool forever:{false,true}){
  Reset();failure=at;persistent=forever;const auto rc=Reboot(detail);(void)rc;
  assert(disk[Boot]==original||(disk.contains(BootBackup)&&disk[BootBackup]==original));++checks;
 }
 Reset();failure=0;reboot_fails=true;assert(Reboot(detail));assert(disk[Boot]==original&&!disk.contains(BootBackup));
 Reset();disk[BootBackup]="existing recovery";assert(Reboot(detail)&&writes==0&&disk[BootBackup]=="existing recovery");
 Reset();disk.erase(MM_CONTENTS_POLICY_PATH);assert(Reboot(detail)&&writes==0&&disk[Boot]==original);
 printf("PASS: %u filesystem fault cases; verified boot backup, rollback, recovery retention and reboot refusal\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-hats-reboot-') as d:
 p=Path(d);(p/'test.cpp').write_text(fixture+body+cases)
 subprocess.run(['g++','-std=c++23','-O2','-UNDEBUG','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
