// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <iostream>
#include <openssl/sha.h>
#include <yyjson.h>
#include "nexus/problem_report_core.hpp"
using u8=uint8_t;using u64=uint64_t;using Result=uint32_t;
constexpr Result Fault=123;
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
#define R_UNLESS(c,r) do{if(!(c))return(r);}while(0)
#define R_TRY(e) do{auto rc_=(e);if(rc_)return rc_;}while(0)
template<class F>struct Scope{F fn;~Scope(){fn();}};
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__COUNTER__){[&]{__VA_ARGS__;}}
std::string operator""_nx(const char* s,size_t n){return {s,n};}
void sha256CalculateHash(void* h,const void* p,size_t n){SHA256((const u8*)p,n,(u8*)h);}
struct Thread {void(*fn)(void*){};void* arg{};};
unsigned starts{},sends{},notifications{};Result send_rc{};bool bad_receipt{},write_receipt_failure{},remove_failure{},sd_failure{};
std::map<std::string,std::vector<u8>> disk;std::vector<std::string> trace;
Result threadStart(Thread*t){++starts;t->fn(t->arg);return 0;}Result threadWaitForExit(Thread*){return 0;}Result threadClose(Thread*){return 0;}
void fatalThrow(Result){assert(false);}
namespace sphaira {struct App{static void Notify(const std::string&){++notifications;}};namespace utils{Result CreateThread(Thread*t,void(*fn)(void*),void*arg,size_t){*t={fn,arg};return 0;}}}
namespace nexus {
constexpr Result Invalid=2,Unavailable=3;
void Log(const char* phase,Result rc,u64 value=0){
 char line[640]{};snprintf(line,sizeof(line),"NM2 2026-11-23T12:00:00Z version=0.2.28 session=0000000000000001 uptime_ms=100 operation=1 task=install event=%s rc=0x%08X value=%llu\n",phase,rc,(unsigned long long)value);
 auto& bytes=disk["/switch/nexusmanager/nexus.log"];bytes.insert(bytes.end(),line,line+strlen(line));
}
inline Result snapshot_result{};
struct Status{unsigned mounted{1},ready{1},state{},result{};};struct Client{Result Query(Status&){return 0;}Result ReportSnapshot(std::string& out){out="usb_live_snapshot_version=1\nusb_live_snapshot_current=1\nusb_completion_failure_wait_ns=123\npassword=private\n";return snapshot_result;}};
namespace lifecycle{bool foreground=true;bool CanPoll(){return foreground;}}
inline u64 armGetSystemTick(){return 123;}inline u64 armTicksToNs(u64 x){return x;}
namespace routing_backup{struct Diagnostic{};struct Status{unsigned flags{1},last_result{};};}
namespace support{
using Bytes=std::vector<u8>;constexpr Result Missing=4;
struct Storage {
 Result Open(){return sd_failure?Fault:0;}
 Result Read(const std::string& p,size_t limit,Bytes& out){if(!disk.contains(p))return Missing;if(disk[p].size()>limit)return Invalid;out=disk[p];return 0;}
 Result List(const std::string& p,std::vector<std::string>& names){names.clear();for(auto&[key,val]:disk)if(key.starts_with(p+"/")&&key.find('/',p.size()+1)==key.npos)names.push_back(key.substr(p.size()+1));return 0;}
 Result Write(const std::string&p,const Bytes& b){trace.push_back("write:"+p);if(write_receipt_failure&&p.ends_with("last-receipt.txt"))return Fault;disk[p]=b;return 0;}
 Result Remove(const std::string&p){trace.push_back("remove:"+p);if(remove_failure)return Fault;disk.erase(p);return 0;}
};Result Folders(Storage&,const char*){return 0;}
}
namespace reports{inline constexpr char Endpoint[]="https://reports.example.invalid/v1/reports";bool Configured();void Stop();}
namespace updates{
inline constexpr char Version[]="0.2.28";
unsigned VersionCode(const char* s){return std::string(s)==Version?2028:0;}
Result SendDiagnosticReport(const std::string& bytes,std::string& receipt,const std::atomic_bool& cancel){
 ++sends;if(cancel||send_rc)return send_rc?send_rc:Fault;
 std::array<u8,32> h{};sha256CalculateHash(h.data(),bytes.data(),bytes.size());std::string id;constexpr char hex[]="0123456789abcdef";for(auto b:h){id+=hex[b>>4];id+=hex[b&15];}
 if(bad_receipt)id[0]=id[0]=='a'?'b':'a';receipt="{\"status\":\"stored\",\"id\":\""+id+"\"}";return 0;
}
}
}
namespace sphaira::nexusapp{Result QueryConsoleRoutingBackupDiagnostic(nexus::routing_backup::Diagnostic&){return 0;}Result QueryConsoleRoutingBackup(nexus::routing_backup::Status&){return 0;}}
