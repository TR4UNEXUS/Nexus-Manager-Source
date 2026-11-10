#include "nexus/usb_import_diagnostic.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using Result=std::uint32_t;
#define R_TRY(x) do{const auto r=(x);if(r)return r;}while(0)
#define R_UNLESS(x,r) do{if(!(x))return r;}while(0)
constexpr Result BadInput=0x1759,Failure=0x7777;
namespace nexus {constexpr Result Unavailable=0x8888;}
struct Service{bool active{};};
struct FsFileSystem{Service s;};
struct NcmContentMetaDatabase{Service s;};
struct NexusVsdStatus{unsigned flags{};};
constexpr unsigned NEXUS_VSD_ACTIVE=1,NEXUS_VSD_ARMED=2;
constexpr int NcmStorageId_SdCard=5;
static std::string fail;static bool bootstrap;static unsigned vsd_flags;
static unsigned sd_handles,db_handles,ncm_refs,ns_refs,ncm_exits,ns_exits;
static std::vector<std::string> calls;
Result Call(const char* name){calls.emplace_back(name);return fail==name?Failure:0;}
bool nexusHasSdBootstrap(const char*,unsigned){return bootstrap;}
const char* envGetLoaderInfo(){return "";}unsigned envGetLoaderInfoSize(){return 0;}
bool serviceIsActive(Service* s){return s->active;}
Result fsOpenSdCardFileSystem(FsFileSystem* s){auto r=Call("sd");if(!r){s->s.active=true;++sd_handles;}return r;}
void fsFsClose(FsFileSystem* s){assert(s->s.active&&sd_handles);s->s.active=false;--sd_handles;}
Result ncmInitialize(){auto r=Call("ncm");if(!r)++ncm_refs;return r;}
void ncmExit(){assert(ncm_refs);--ncm_refs;++ncm_exits;}
namespace ns{Result Initialize(){auto r=Call("ns");if(!r)++ns_refs;return r;}void Exit(){assert(ns_refs);--ns_refs;++ns_exits;}}
Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase* db,int id){assert(id==5);auto r=Call("db");if(!r){db->s.active=true;++db_handles;}return r;}
void ncmContentMetaDatabaseClose(NcmContentMetaDatabase* db){if(db->s.active){assert(db_handles);--db_handles;db->s.active=false;}}
struct Usb{Result VirtualSdStatus(NexusVsdStatus& s){auto r=Call("vsd");s.flags=vsd_flags;return r;}};
struct Disk {
 FsFileSystem sd{};NcmContentMetaDatabase db{};bool ncm_open{},ns_open{};Usb usb;const char* step{};
 // PRODUCTION_DESTRUCTOR
 // PRODUCTION_OPEN
};
void Reset(){fail.clear();bootstrap=false;vsd_flags=0;sd_handles=db_handles=0;ncm_refs=3;ns_refs=2;ncm_exits=ns_exits=0;calls.clear();}
void Balanced(){assert(sd_handles==0&&db_handles==0&&ncm_refs==3&&ns_refs==2);}
int main(){
 for(const std::string stage:{"vsd","sd","ncm","ns","db"}){
  Reset();fail=stage;
  {Disk d;assert(d.Open(true)==Failure);assert(d.step&&std::strlen(d.step));auto detail=nexus::usbimport::FailureDetail(d.step,BadInput);assert(detail.find(d.step)!=std::string::npos&&detail.find("0x00001759")!=std::string::npos);}
  Balanced();
  const std::vector<std::string> order{"vsd","sd","ncm","ns","db"};
  const auto pos=std::find(order.begin(),order.end(),stage)-order.begin();
  assert(calls==std::vector<std::string>(order.begin(),order.begin()+pos+1));
  assert(ncm_exits==(pos>2?1u:0u)&&ns_exits==(pos>3?1u:0u));
 }
 Reset();{Disk d;assert(d.Open(true)==0&&ncm_refs==4&&ns_refs==3&&db_handles==1&&sd_handles==1);}Balanced();assert(ncm_exits==1&&ns_exits==1);
 Reset();{Disk d;assert(d.Open(false)==0&&ncm_refs==3&&ns_refs==2&&db_handles==0&&sd_handles==1);}Balanced();assert((calls==std::vector<std::string>{"vsd","sd"}));
 for(unsigned flags:{NEXUS_VSD_ACTIVE,NEXUS_VSD_ARMED,NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED}){Reset();vsd_flags=flags;{Disk d;assert(d.Open(true)==nexus::Unavailable&&sd_handles==0);}Balanced();assert((calls==std::vector<std::string>{"vsd"}));}
 Reset();bootstrap=true;{Disk d;assert(d.Open(true)==0);}Balanced();assert(calls.front()=="sd");
 auto reason=nexus::usbimport::FailureDetail("checking system identity",BadInput,"Keep the pending backup.");
 assert(reason.starts_with("Keep the pending backup.\n\n")&&reason.find("0x00001759")!=std::string::npos);
 std::cout<<"USB import service ownership and diagnostic tests passed\n";
}

