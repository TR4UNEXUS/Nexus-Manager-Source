#!/usr/bin/env python3
"""Check actual diagnostic log retention and rotation in a temporary directory."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'sphaira/source/nexus/diagnostics.cpp').read_text()
source=source[:source.index('namespace diagnostics {\nnamespace {\nconstexpr Result Missing')]+"\n}\n"
source=re.sub(r'^#include [^\n]+\n','',source,flags=re.M)
fixture=r'''
#include <atomic>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <string>
using u64=unsigned long long;using Result=unsigned;
u64 armGetSystemTick(){static u64 tick=100;return ++tick;}
u64 armTicksToNs(u64 t){return t*1000;}
namespace updates{constexpr const char*Version="0.2.27";}
namespace nexus{void Log(const char*,Result,u64=0);namespace diagnostics{
class Operation{u64 previous{};const char*previous_name{};bool previous_failure{};
public:explicit Operation(const char*);~Operation();};}}
'''
cases=r'''
using namespace nexus;
std::string Read(const std::string&p){std::ifstream f("sdmc:/switch/nexusmanager/"+p);return {std::istreambuf_iterator<char>(f),{}};}
void Write(const std::string&p,const std::string&s){std::ofstream f("sdmc:/switch/nexusmanager/"+p);f<<s;}
int main(){
 std::filesystem::create_directories("sdmc:/switch/nexusmanager");
 Write("first-result.log","old first result");Log("normal",0);assert(Read("first-result.log")=="old first result");
 {diagnostics::Operation op("install");Log("first\nfailed",0x123,7);Log("later",0x234,8);
  {diagnostics::Operation child("nested");Log("child-fault",0x345,9);}Log("later-again",0x456,10);}
 assert(Read("first-result.previous.log")=="old first result");
 assert(Read("first-result.log").find("event=first_failed rc=0x00000123")!=std::string::npos);
 assert(Read("first-result.log").find("later")==std::string::npos);
 auto failures=Read("operation-failures.log");assert(failures.find("first_failed")!=std::string::npos&&failures.find("child-fault")!=std::string::npos&&failures.find("later")==std::string::npos);
 Write("nexus.log",std::string(1024*1024+1,'x'));Log("after-rotation",0);assert(Read("nexus.previous.log").size()==1024*1024+1&&Read("nexus.log").find("after-rotation")!=std::string::npos);
 // A blocked rename must leave the existing first-result file intact.
 first_saved=first_rotated=false;std::filesystem::remove("sdmc:/switch/nexusmanager/first-result.previous.log");
 std::filesystem::create_directory("sdmc:/switch/nexusmanager/first-result.previous.log");
 Write("first-result.previous.log/keep","occupied");const auto saved=Read("first-result.log");Log("blocked-rotation",0x555);assert(Read("first-result.log")==saved&&!first_saved&&!first_rotated);
 puts("PASS: UTC/version/operation logs, first session/operation results, nested scopes, bounded rotation, and failed-rotation preservation.");
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-logs-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture+source+cases)
 for opt in ('-O0','-O2'):
  run=p/opt;run.mkdir()
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],cwd=run,check=True)
