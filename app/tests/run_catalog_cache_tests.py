#!/usr/bin/env python3
"""Fault-test production catalogue selection/update; crypto has its separate suite."""
from pathlib import Path
import subprocess,tempfile
A=Path(__file__).resolve().parents[1]
p=(A/'sphaira/source/nexus/repair_catalog.cpp').read_text()
p=p[p.index('Result LoadInternal('):]
fixture=r'''
#include "nexus/repair_catalog_core.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <iostream>
using Result=unsigned;using u64=uint64_t;
#define R_FAILED(x) ((x)!=0)
#define R_TRY(x) do{auto rc_=(x);if(rc_)return rc_;}while(0)
#define R_UNLESS(c,r) do{if(!(c))return(r);}while(0)
template<class F>struct Scope{F fn;~Scope(){fn();}};
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__COUNTER__){[&]{__VA_ARGS__;}}
std::string operator""_nx(const char*s,size_t n){return {s,n};}
unsigned romfsInit(){return 0;}void romfsExit(){}
FILE* BundleFile(const char*,const char*){auto*f=tmpfile();unsigned char b=1;fwrite(&b,1,1,f);rewind(f);return f;}
#define fopen BundleFile
namespace sphaira::ui{class ProgressBox{};}
namespace nexus{
constexpr Result Invalid=2,Fault=3;
void Log(const char*,Result,u64){}
namespace support{
using Bytes=std::vector<uint8_t>;constexpr Result Missing=4;std::map<std::string,Bytes>disk;bool fail_open{},fail_write{},partial{},fail_read{};unsigned writes{};
struct Storage{
 Result Open(){return fail_open?Fault:0;}
 Result Read(const std::string&p,size_t,Bytes& b){if(!disk.contains(p))return Missing;if(fail_read)return Fault;b=disk[p];return 0;}
 Result Write(const std::string&p,const Bytes& b){++writes;if(partial){disk[p]={0};return Fault;}if(fail_write)return Fault;disk[p]=b;return 0;}
};Result Folders(Storage&,const char*){return 0;}
}
namespace updates{Result fetch{};std::vector<uint8_t>download{2};Result FetchRepairCatalog(sphaira::ui::ProgressBox*,std::vector<uint8_t>& b,std::string&){b=download;return fetch;}}
namespace catalog{
struct Loaded{Catalog catalog;Bytes bytes;bool cached{},cache_rejected{};};
namespace{
std::mutex mutex;
constexpr const char* Slots[]{"/catalog/0","/catalog/1"};
bool DecodeBytes(const Bytes& bytes,Catalog& out){if(bytes.empty()||!bytes[0]||bytes[0]>5)return false;out.revision=bytes[0];return true;}
'''
cases=r'''
}
using namespace nexus;using namespace nexus::catalog;
int main(){
 Loaded out;std::string detail;assert(!Load(out)&&out.catalog.revision==1&&!out.cached);
 support::fail_open=true;assert(!Load(out)&&out.catalog.revision==1&&out.cache_rejected);support::fail_open=false;
 support::disk[Slots[0]]={2};support::disk[Slots[1]]={0};assert(!Load(out)&&out.catalog.revision==2&&out.cached&&out.cache_rejected);
 updates::download={3};for(bool partial:{false,true}){support::fail_write=!partial;support::partial=partial;assert(Update(nullptr,detail)==Fault);support::fail_write=support::partial=false;assert(!Load(out)&&out.catalog.revision==2&&support::disk[Slots[0]]==Bytes{2});}
 assert(!Update(nullptr,detail));assert(!Load(out)&&out.catalog.revision==3&&support::disk[Slots[0]]==Bytes{2});
 auto saved=support::disk;const auto writes=support::writes;updates::download={2};assert(Update(nullptr,detail)==Invalid&&support::disk==saved&&support::writes==writes);
 updates::download={3,42};assert(Update(nullptr,detail)==Invalid&&support::disk==saved);updates::download={0};assert(Update(nullptr,detail)==Invalid&&support::disk==saved);
 updates::download={4};updates::fetch=Fault;assert(Update(nullptr,detail)==Fault&&support::disk==saved);updates::fetch=0;
 support::disk[Slots[0]]={3,42};assert(!Load(out)&&out.cache_rejected&&out.catalog.revision==3);
 support::disk.clear();support::disk[Slots[0]]={1,42};assert(!Load(out)&&out.catalog.revision==1&&out.cache_rejected&&!out.cached);
 std::cout<<"PASS: production catalogue cache selection, offline fallback, rollback/equivocation rejection and failed inactive-slot writes preserve the valid catalogue.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-catalog-cache-') as tmp:
 T=Path(tmp);(T/'test.cpp').write_text(fixture+p+cases)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-sign-compare','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(A/'sphaira/include'),str(T/'test.cpp'),'-o',str(T/'test')],check=True)
  subprocess.run([str(T/'test')],check=True)
