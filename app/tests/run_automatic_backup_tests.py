#!/usr/bin/env python3
"""Exercise the real automatic-backup coordinator with faulting service boundaries."""
from pathlib import Path
import subprocess, tempfile, re
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/routing_backup_auto.cpp').read_text()
source=re.sub(r'^#include [^\n]+\n','',source,flags=re.M)
fixture=r'''
#include <atomic>
#include <mutex>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>
#include <cassert>
#include <stdexcept>
using u64=unsigned long long;using Result=unsigned;
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
#define R_TRY(x) do{auto r=(x);if(r)return r;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return r;}while(0)
#define MAKERESULT(m,d) ((m)|((d)<<9))
struct Scope{template<class F>Scope(F f):fn(f){}std::function<void()>fn;~Scope(){fn();}};
#define ON_SCOPE_EXIT(x) Scope scope([&]{x;})
constexpr unsigned FS_MAX_PATH=1024,NEXUS_VSD_ACTIVE=1,NEXUS_VSD_ARMED=2;
struct FsFileSystem{};enum FsDirEntryType{FsDirEntryType_File,FsDirEntryType_Dir};
struct NexusVsdStatus{unsigned flags{};};
struct Thread{std::thread t;void(*fn)(void*){};};
bool bootstrap=true,can_poll=true;unsigned flags{},vsd_rc{},open_rc{},entry_rc{},missing{},is_dir{},confirm_rc{},confirm_calls{},log_calls{},create_rc{},start_rc{},closes{};
std::vector<std::string>notices;
const char* envGetLoaderInfo(){return nullptr;}unsigned envGetLoaderInfoSize(){return 0;}
bool nexusHasSdBootstrap(const char*,unsigned){return bootstrap;}
Result fsOpenSdCardFileSystem(FsFileSystem*){return open_rc;}
void fsFsClose(FsFileSystem*){++closes;}
Result fsFsGetEntryType(FsFileSystem*,const char*p,FsDirEntryType*t){unsigned i=p[1]-'0'+1;*t=is_dir==i?FsDirEntryType_Dir:FsDirEntryType_File;return (missing==i||missing==4)?MAKERESULT(2,1):entry_rc;}
Result threadStart(Thread*t){if(start_rc)return start_rc;t->t=std::thread(t->fn,nullptr);return 0;}
Result threadWaitForExit(Thread*t){t->t.join();return 0;}Result threadClose(Thread*){return 0;}
void fatalThrow(Result){assert(false);}
namespace utils{Result CreateThread(Thread*t,void(*fn)(void*),void*,unsigned size){assert(size>=65536);t->fn=fn;return create_rc;}}
namespace nexus{constexpr Result Unavailable=99,Invalid=100;struct Client{Result VirtualSdStatus(NexusVsdStatus&v){v.flags=flags;return vsd_rc;}};
void Log(const char*,Result,u64){++log_calls;}
namespace repair{std::string Live(unsigned i){return "/"+std::to_string(i);}}
namespace lifecycle{bool CanPoll(){return can_poll;}}}
namespace sphaira::nexusapp{Result ConfirmConsoleRoutingBackup(u64&s){++confirm_calls;s=confirm_rc?0:42;return confirm_rc;}}
struct App{static void Notify(const std::string&s){notices.push_back(s);}};
std::string operator""_nx(const char*s,size_t n){return {s,n};}
'''
fixture=fixture.replace('#include <atomic>','#include <functional>\n#include <atomic>')
cases=r'''
using namespace sphaira::nexusapp;
void Reset(){StopAutomaticRoutingBackup();attempted=started=false;done=false;notice_pending=false;notice_result=0;warned=false;last_notice=0;
 bootstrap=can_poll=true;flags=vsd_rc=open_rc=entry_rc=missing=is_dir=confirm_rc=confirm_calls=log_calls=create_rc=start_rc=closes=0;notices.clear();}
void Boot(){PollAutomaticRoutingBackup();StopAutomaticRoutingBackup();PollAutomaticRoutingBackup();}
int main(){
 Reset();Boot();assert(confirm_calls==1&&log_calls==1&&notices.empty()&&closes==1);
 for(int i=0;i<20;++i)PollAutomaticRoutingBackup();assert(confirm_calls==1);
 BackupConsoleRoutingAfterChange();PollAutomaticRoutingBackup();assert(confirm_calls==2&&notices.empty());
 for(unsigned i=1;i<=3;++i){Reset();missing=i;Boot();assert(!confirm_calls&&log_calls==1&&notices.size()==1);}
 Reset();missing=4;Boot();assert(!confirm_calls&&!log_calls&&notices.empty());
 for(unsigned failure=1;failure<=6;++failure){Reset();
  if(failure==1)open_rc=10;if(failure==2)entry_rc=11;if(failure==3)is_dir=2;
  if(failure==4)confirm_rc=12;if(failure==5)create_rc=13;if(failure==6)start_rc=14;
  Boot();assert(notices.size()==1);assert(confirm_calls==unsigned(failure==4));
 }
 Reset();confirm_rc=12;Boot();assert(notices.size()==1);BackupConsoleRoutingAfterChange();PollAutomaticRoutingBackup();assert(notices.size()==1);
 confirm_rc=0;BackupConsoleRoutingAfterChange();PollAutomaticRoutingBackup();assert(notices.size()==2&&notices.back().find("saved and checked")!=std::string::npos);
 for(unsigned flag:{NEXUS_VSD_ACTIVE,NEXUS_VSD_ARMED}){Reset();bootstrap=false;flags=flag;Boot();assert(!confirm_calls&&notices.size()==1);}
 Reset();bootstrap=false;vsd_rc=90;Boot();assert(!confirm_calls&&notices.size()==1);
 Reset();bootstrap=false;Boot();assert(confirm_calls==1&&notices.empty());
 Reset();can_poll=false;PollAutomaticRoutingBackup();assert(!attempted&&!confirm_calls);can_poll=true;Boot();assert(confirm_calls==1);
 puts("PASS: automatic startup backup once, post-change saves, absent maps preserved, physical SD guard, service/thread faults, deduplicated warnings and recovery notification.");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-auto-backup-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture+source+cases)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
