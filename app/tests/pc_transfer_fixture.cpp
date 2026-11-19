#include "nexus/transfer_policy.hpp"
#include "nexus/transfer_activity.hpp"
#include "nexus/ftp_stream.hpp"
#include <future>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <thread>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;using Result=u32;
#define MAKERESULT(m,d) (u32((d)*512+(m)))
#define R_TRY(x) do{const Result rc_=(x);if(rc_)return rc_;}while(false)
#define R_UNLESS(c,r) do{if(!(c))return r;}while(false)
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
constexpr int Module_Libnx=345,LibnxError_BadInput=4,FS_MAX_PATH=769;
constexpr int FsCreateOption_BigFile=1,FsOpenMode_Write=2,FsOpenMode_Append=4,FsOpenMode_Read=1,FsWriteOption_None=0,FsReadOption_None=0,FsDirOpenMode_ReadDirs=1,FsDirOpenMode_ReadFiles=2;
enum FsDirEntryType {FsDirEntryType_Dir,FsDirEntryType_File};
struct FsDirectoryEntry {char name[769]{};FsDirEntryType type{};s64 file_size{};};
struct FsFileSystem {};
struct FsFile {std::string path;bool live{};};
struct FsDir {std::vector<FsDirectoryEntry> entries;size_t pos{};bool live{};};
namespace fake {
constexpr Result MissingSd=MAKERESULT(2,1),MissingUsb=MAKERESULT(140,205),Io=99,Changed=98;
struct Disk {std::map<std::string,std::vector<u8>> files;std::set<std::string> dirs{"/","/switch","/switch/nexusmanager","/switch/nexusmanager/transfers","/switch/nexusmanager/transfers/install"};size_t reads{},writes{},renames{},flushes{},creates{};};
inline Disk sd,usb;inline u64 generation=42;inline bool fail_delete{},fail_write{},fail_rename{},fail_flush{},mounted=true,can_poll=true;
inline int ncm_refs{},ncm_opens{},ncm_closes{};inline Result ncm_error{},firmware_error{},mutation_error{},query_error{};
inline std::map<std::string,Result> log;
Result Type(Disk& d,const std::string& p,FsDirEntryType* t,Result missing){if(d.dirs.contains(p)){*t=FsDirEntryType_Dir;return 0;}if(d.files.contains(p)){*t=FsDirEntryType_File;return 0;}return missing;}
Result Create(Disk& d,const std::string& p,s64 n,bool replace=false){if((d.files.contains(p)&&!replace)||d.dirs.contains(p))return Io;assert(n>=0&&n<10000);d.files[p]=std::vector<u8>(n);++d.creates;return 0;}
Result Write(Disk& d,const std::string& p,s64 off,const void* b,u64 n){if(fail_write)return Io;auto it=d.files.find(p);if(it==d.files.end())return Io;assert(off>=0&&off+n<10000);auto& v=it->second;v.resize(std::max<u64>(v.size(),off+n));memcpy(v.data()+off,b,n);++d.writes;return 0;}
Result Read(Disk& d,const std::string& p,s64 off,void* b,u64 n,u64* got){auto it=d.files.find(p);if(it==d.files.end())return Io;assert(off>=0&&u64(off)<=it->second.size());*got=std::min<u64>(n,it->second.size()-off);if(*got)memcpy(b,it->second.data()+off,*got);++d.reads;return 0;}
Result Rename(Disk& d,const std::string& a,const std::string& b){if(fail_rename||!d.files.contains(a)||d.files.contains(b))return Io;d.files[b]=std::move(d.files[a]);d.files.erase(a);++d.renames;return 0;}
std::vector<FsDirectoryEntry> Entries(Disk& d,const std::string& p){std::vector<FsDirectoryEntry> out;for(const auto& [name,data]:d.files)if(name.starts_with(p+"/")&&name.find('/',p.size()+1)==name.npos){FsDirectoryEntry e{};snprintf(e.name,sizeof(e.name),"%s",name.substr(p.size()+1).c_str());e.file_size=data.size();e.type=FsDirEntryType_File;out.push_back(e);}return out;}
}
Result fsFsGetEntryType(FsFileSystem*,const char* p,FsDirEntryType* t){return fake::Type(fake::sd,p,t,fake::MissingSd);}
Result fsFsGetTotalSpace(FsFileSystem*,const char*,s64* n){*n=1LL<<30;return 0;}
Result fsFsGetFreeSpace(FsFileSystem*,const char*,s64* n){*n=1LL<<29;return 0;}
Result fsFsCreateDirectory(FsFileSystem*,const char* p){fake::sd.dirs.insert(p);return 0;}
Result fsFsCreateFile(FsFileSystem*,const char* p,s64 n,int){return fake::Create(fake::sd,p,n);}
Result fsFsOpenFile(FsFileSystem*,const char* p,int,FsFile* f){if(!fake::sd.files.contains(p))return fake::MissingSd;*f={p,true};return 0;}
Result fsFileGetSize(FsFile* f,s64* n){assert(f->live);*n=fake::sd.files.at(f->path).size();return 0;}
Result fsFileWrite(FsFile* f,s64 o,const void* b,u64 n,int){assert(f->live);return fake::Write(fake::sd,f->path,o,b,n);}
Result fsFileRead(FsFile* f,s64 o,void* b,u64 n,int,u64* got){assert(f->live);return fake::Read(fake::sd,f->path,o,b,n,got);}
Result fsFileFlush(FsFile* f){assert(f->live);++fake::sd.flushes;return fake::fail_flush?fake::Io:0;}
void fsFileClose(FsFile* f){assert(f->live);f->live=false;}
Result fsFsCommit(FsFileSystem*){++fake::sd.flushes;return fake::fail_flush?fake::Io:0;}
Result fsFsDeleteFile(FsFileSystem*,const char* p){if(fake::fail_delete)return fake::Io;return fake::sd.files.erase(p)?0:fake::MissingSd;}
Result fsFsRenameFile(FsFileSystem*,const char* a,const char* b){return fake::Rename(fake::sd,a,b);}
Result fsFsOpenDirectory(FsFileSystem*,const char* p,int,FsDir* d){if(!fake::sd.dirs.contains(p))return fake::MissingSd;*d={fake::Entries(fake::sd,p),0,true};return 0;}
Result fsDirRead(FsDir* d,s64* n,size_t count,FsDirectoryEntry* e){assert(d->live&&count==1);*n=d->pos<d->entries.size();if(*n)*e=d->entries[d->pos++];return 0;}
void fsDirClose(FsDir* d){assert(d->live);d->live=false;}
Result ncmInitialize(){++fake::ncm_opens;if(fake::ncm_error)return fake::ncm_error;++fake::ncm_refs;return 0;}
void ncmExit(){assert(fake::ncm_refs>0);--fake::ncm_refs;++fake::ncm_closes;}
namespace nexus {
constexpr Result Unavailable=97;
namespace lifecycle {bool CanPoll(){return fake::can_poll;}}
void Log(const char* phase,Result r=0,u64=0){fake::log[phase]=r;}
Result CheckUsbFirmware(){assert(fake::ncm_refs>0);return fake::firmware_error;}
Result CheckUsbMutationAllowed(){assert(fake::ncm_refs>0);return fake::mutation_error;}
struct Status {u64 generation{};bool ready{};bool Available()const{return generation&&ready;}};
struct Space {u64 free{},total{};};struct Info {u64 size{};bool directory{};};struct Entry {char name[256]{};u64 size{};bool directory{};};
struct Client {
 Result Query(Status& s){s={fake::generation,fake::mounted};return fake::query_error;}
 Result Check(u64 g){return g&&fake::mounted&&g==fake::generation?0:fake::Changed;}
 Result Stat(u64 g,const std::string& p,Info& i){R_TRY(Check(g));FsDirEntryType type;R_TRY(fake::Type(fake::usb,p,&type,fake::MissingUsb));i={type==FsDirEntryType_File?fake::usb.files.at(p).size():0,type==FsDirEntryType_Dir};return 0;}
 Result GetSpace(u64 g,Space& s){R_TRY(Check(g));s={1ULL<<29,1ULL<<30};return 0;}
 Result CreateDirectory(u64 g,const std::string& p){R_TRY(Check(g));fake::usb.dirs.insert(p);return 0;}
 Result CreateFile(u64 g,const std::string& p,s64 n){R_TRY(Check(g));return fake::Create(fake::usb,p,n,true);}
 Result Write(u64 g,const std::string& p,s64 o,const void* b,u64 n){R_TRY(Check(g));return fake::Write(fake::usb,p,o,b,n);}
 Result Read(u64 g,const std::string& p,s64 o,void* b,u64 n,u64& got){R_TRY(Check(g));return fake::Read(fake::usb,p,o,b,n,&got);}
 Result DeleteInstallTemporary(u64 g,const std::string& p,u64 size){R_TRY(Check(g));if(fake::fail_delete)return fake::Io;Info info{};R_TRY(Stat(g,p,info));R_UNLESS(nexus::transfer::TemporaryInstallPath(p)&&!info.directory&&size&&info.size==size,fake::Io);fake::usb.files.erase(p);return 0;}
 Result RenameNew(u64 g,const std::string& a,const std::string& b){R_TRY(Check(g));return fake::Rename(fake::usb,a,b);}
 Result Flush(u64 g){R_TRY(Check(g));++fake::usb.flushes;return fake::fail_flush?fake::Io:0;}
 Result List(u64 g,const std::string& p,std::vector<Entry>& out){R_TRY(Check(g));for(const auto& e:fake::Entries(fake::usb,p)){Entry item;snprintf(item.name,sizeof(item.name),"%s",e.name);item.size=e.file_size;item.directory=e.type==FsDirEntryType_Dir;out.push_back(item);}return 0;}
};
namespace transfer {
enum class Mode {Off,Cable,Ftp};
struct InstallJob {bool usb{};u64 generation{},size{};std::string path;std::shared_ptr<FtpStream> stream;u64 upload_id{};};
// AREA_PRODUCTION
std::vector<std::shared_ptr<Area>> areas;
enum FtpVfsOpenMode {FtpVfsOpenMode_READ,FtpVfsOpenMode_WRITE,FtpVfsOpenMode_APPEND};
// BRIDGE_PRODUCTION
// INSTALL_QUEUE_PRODUCTION
u64 ProbeUsb(){
// USB_PREFLIGHT_PRODUCTION
return usbPin;
}
}
}
using namespace nexus::transfer;
static void Reset(){fake::sd={};fake::usb={};fake::generation=42;fake::mounted=fake::can_poll=true;fake::fail_write=fake::fail_flush=fake::fail_rename=false;fake::ncm_refs=fake::ncm_opens=fake::ncm_closes=0;fake::ncm_error=fake::firmware_error=fake::mutation_error=fake::query_error=0;fake::log.clear();jobs.clear();activeUpload={};fake::fail_delete=false;ftpStream.reset();areas.clear();installBusy=installFailed=false;received=sent=completed=0;fsOpen=true;usbProblem.clear();mode=Mode::Ftp;activity::Get().Start("Test session");}
static std::string FtpPath(Use use,const std::string& leaf={}){return "/"+std::string(VolumeName(use))+":/"+leaf;}
// Explicit staged-owner adapter for cable/staging regression tests.
int staged_open(void* user,const char* p,FtpVfsOpenMode m){
 auto& f=*static_cast<File**>(user);f=nullptr;std::string name;auto* a=Route(p,name,m!=FtpVfsOpenMode_READ);
 if(!a||m==FtpVfsOpenMode_APPEND)return -1;
 return ftp_result(m==FtpVfsOpenMode_WRITE?a->ReceiveFtp(name,&f):a->Open(name,false,-1,&f));
}
void StreamingTests(){
 using namespace std::chrono_literals;
 for(auto use:{Use::InstallSd,Use::InstallUsb})for(const auto* extension:{"nsp","NSZ","xci","XCZ"}){
  Reset();areas.push_back(std::make_shared<Area>(use,use==Use::InstallUsb?42:0));
  const auto path=FtpPath(use,std::string("Streaming.")+extension);
  auto& disk=use==Use::InstallUsb?fake::usb:fake::sd;
  const auto old=std::string(Base(use))+"/Streaming."+extension;
  disk.files[old]={'o','l','d'};disk.files[old+".partial"]={'k'};
  const auto saved=disk.files;
  struct stat stat{};assert(bridge_stat(path.c_str(),&stat)<0&&errno==ENOENT);
  Dir* listing{};assert(!bridge_opendir(&listing,FtpPath(use).c_str()));
  assert(std::string(bridge_readdir(&listing,nullptr))==InstallGuideName&&!bridge_readdir(&listing,nullptr));assert(!bridge_closedir(&listing));
  File* f{};assert(!bridge_open(&f,path.c_str(),FtpVfsOpenMode_WRITE));assert(f->stream&&jobs.size()==1);
  File* conflict{};assert(bridge_open(&conflict,path.c_str(),FtpVfsOpenMode_WRITE)<0&&!conflict);
  InstallJob job;assert(TakeInstall(job));assert(job.stream&&job.usb==(use==Use::InstallUsb)&&job.generation==(job.usb?42:0));
  // Consumer starts before EOF. Real FTP VFS writes feed the same bounded
  // pipe for both immutable targets; neither filesystem receives a package.
  std::promise<void> first;std::atomic_bool eof{},allow_commit{};
  std::vector<u8> payload(9*1024*1024+17);for(size_t i=0;i<payload.size();++i)payload[i]=i%251;
  std::vector<u8> installed_bytes;
  auto consumer=std::async(std::launch::async,[&]{
   std::vector<u8> block(73129);u64 got{};bool started{};
   for(;;){assert(!job.stream->Read(block.data(),block.size(),got,[]{return Result(0);}));if(!got)break;
    installed_bytes.insert(installed_bytes.end(),block.begin(),block.begin()+got);
    if(!started){assert(!eof);first.set_value();started=true;}
   }
   while(!allow_commit)std::this_thread::yield();job.stream->Complete(0);
  });
  assert(bridge_write(&f,payload.data(),1024)==1024);first.get_future().wait();
  assert(bridge_write(&f,payload.data()+1024,payload.size()-1024)==int(payload.size()-1024));
  eof=true;auto finish=std::async(std::launch::async,[&]{return bridge_finish(&f);});
  assert(finish.wait_for(30ms)==std::future_status::timeout); // no premature FTP success
  allow_commit=true;assert(finish.get()==0);consumer.get();FinishInstall(0);
  assert(installed_bytes==payload&&disk.files==saved&&fake::sd.creates==0&&fake::usb.creates==0);
  assert(!ftpStream&&!installBusy&&activity::Get().Read().installed==1);
  // The next package can start before the old UI completion callback runs.
  installBusy=true;File* next{};assert(!bridge_open(&next,path.c_str(),FtpVfsOpenMode_WRITE));
  assert(bridge_close(&next)<0);assert(installFailed);installBusy=false;
 }
 // An aborted upload unblocks a waiting consumer and cannot be successful.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));File* f{};
 assert(!bridge_open(&f,FtpPath(Use::InstallUsb,"Abort.nsp").c_str(),FtpVfsOpenMode_WRITE));
 InstallJob job;assert(TakeInstall(job));auto read=std::async(std::launch::async,[&]{u8 byte;u64 n;return job.stream->Read(&byte,1,n,[]{return Result(0);});});
 assert(bridge_close(&f)<0&&read.get()!=0);job.stream->Complete(99);FinishInstall(99);
 assert(fake::sd.files.empty()&&fake::usb.files.empty()&&!activity::Get().Read().installed);
 // Destination unavailable at open never creates a stream or falls back to SD.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));fake::mounted=false;
 assert(bridge_open(&f,FtpPath(Use::InstallUsb,"Missing.nsp").c_str(),FtpVfsOpenMode_WRITE)<0&&!f&&jobs.empty());
 // Stop/worker failure wakes a producer blocked by backpressure.
 FtpStream small(32);std::vector<u8> large(128,7);
 auto writer=std::async(std::launch::async,[&]{return small.Write(large.data(),0,large.size());});
 while(small.Received()!=32)std::this_thread::yield();small.Cancel(1234);assert(writer.get()==1234);
 FtpStream incomplete;assert(!incomplete.Write("abc",0,3));incomplete.Complete(0);assert(incomplete.WaitResult()!=0);
 std::cout<<"Actual FTP bridge: SD/USB live streaming before EOF, all formats, no staged package, backpressure, sequential uploads, aborts, commit acknowledgement and target isolation passed\n";
}
void CleanupTests(){
 for(auto use:{Use::InstallSd,Use::InstallUsb})for(int test=0;test<9;++test){
  Reset();areas.push_back(std::make_shared<Area>(use,use==Use::InstallUsb?42:0));
  auto& disk=use==Use::InstallUsb?fake::usb:fake::sd;auto& other=use==Use::InstallUsb?fake::sd:fake::usb;
  const auto base=std::string(Base(use));disk.files[base+"/Old.nsp"]={'o'};disk.files[base+"/Other.partial"]={'p'};
  File* file{};assert(staged_open(&file,FtpPath(use,"New.nsp").c_str(),FtpVfsOpenMode_WRITE)==0);
  assert(bridge_write(&file,"content",7)==7&&bridge_finish(&file)==0);
  InstallJob job;assert(TakeInstall(job)&&job.upload_id&&job.size==7);
  const auto path=job.path;other.files[path]={'x'};const auto other_before=other.files;
  if(test==1)job.upload_id=0;
  if(test==2)job.path=base+"/Old.nsp";
  if(test==3)job.size=8;
  if(test==4)fake::fail_delete=true;
  if(test==5)fake::can_poll=false;
  if(test==6)activeUpload={}; // Stop invalidates ownership before closing storage.
  if(test==7){if(job.usb)++fake::generation;else fsOpen=false;}
  if(test==8)disk.files[path].push_back('!');
  const auto rc=RemoveInstalledUpload(job);
  assert((rc==0)==(test==0));assert(disk.files.contains(path)==(test!=0));
  assert(disk.files.at(base+"/Old.nsp")==std::vector<u8>{'o'});
  assert(disk.files.at(base+"/Other.partial")==std::vector<u8>{'p'}&&other.files==other_before);
  if(!test){disk.files[path]={'r','e','p','l','a','c','e'};assert(RemoveInstalledUpload(job));assert(disk.files.contains(path));}
  FinishInstall(0);
 }
 for(unsigned rc:{0u,1u,99u})for(size_t installed:{0,1,4})for(size_t skipped:{0,1,3})
  assert(InstalledEntirePackage(rc,installed,skipped)==(!rc&&installed>0&&!skipped));
 for(const auto* p:{"sdmc:/switch/nexusmanager/transfers/install/a.nsp","SDMC:/SWITCH/NEXUSMANAGER/TRANSFERS/INSTALL/a.NSP","sdmc:/switch","sdmc:/switch/nexusmanager/transfers","sdmc:/switch/nexusm~1/transfers/install/a.nsp","sdmc:/switch/nexusmanager./transfers/install/a.nsp","sdmc:/games/../switch/nexusmanager/transfers/install/a.nsp"})assert(!SdMutationAllowed(p));
 for(const auto* p:{"sdmc:/games/a.nsp","sdmc:/games/a.partial","sdmc:/switch/Other/app.nro","sdmc:/switch/nexusmanager/transfers/ordinary.nsp"})assert(SdMutationAllowed(p));
 for(const auto* p:{"/games/a.nsp","/switch/nexusmanager/transfers/install","/switch/nexusmanager/transfers/install/a.partial","/switch/nexusmanager/transfers/install/a/../a.nsp"})assert(!TemporaryInstallPath(p));
 std::cout<<"Successful staged cleanup, ownership, stale media, failures, unrelated files and raw FTP staging guards passed\n";
}
int main(){CleanupTests();
 StreamingTests();
 for(auto use:{Use::InstallSd,Use::InstallUsb}){
  Reset();areas.push_back(std::make_shared<Area>(use,use==Use::InstallUsb?42:0));
  for(const auto* ext:{"nsp","NSZ","xci","XCZ"}){
   File* f{};const auto path=FtpPath(use,std::string("Game update.")+ext);assert(staged_open(&f,path.c_str(),FtpVfsOpenMode_WRITE)==0&&f);
   assert(jobs.empty()&&bridge_write(&f,"test-data",9)==9);assert(jobs.empty());
   assert(bridge_finish(&f)==0&&!f&&jobs.size()==1);
   auto live=activity::Get().Read();assert(live.queued==1&&!live.installing&&live.upload.bytes==9);
   InstallJob job;assert(TakeInstall(job));assert(job.usb==(use==Use::InstallUsb)&&job.generation==(job.usb?42:0)&&job.size==9);
   live=activity::Get().Read();assert(live.installing&&live.queued==0&&live.install_target==(job.usb?"USB":"SD"));
   const auto before=live.installed;FinishInstall(0);assert(activity::Get().Read().installed==before+1);
   auto& disk=job.usb?fake::usb:fake::sd;assert(disk.files.contains(job.path)&&!disk.files.contains(job.path+".partial"));
  }
  assert((use==Use::InstallUsb?fake::sd:fake::usb).files.empty());
  for(const auto* leaf:{"bad.exe","../game.nsp","folder/game.nsp","game.nsp.partial",InstallGuideName}){File* f{};assert(staged_open(&f,FtpPath(use,leaf).c_str(),FtpVfsOpenMode_WRITE)<0&&!f);}
  // The readable guide is virtual, immutable, seekable and never queued.
  const auto calls=fake::sd.reads+fake::usb.reads;File* f{};const auto guide=FtpPath(use,InstallGuideName);
  assert(staged_open(&f,guide.c_str(),FtpVfsOpenMode_READ)==0);char text[4096]{};const auto n=bridge_read(&f,text,sizeof(text));assert(n>0&&std::string(text,n).find("Drag and drop")!=std::string::npos);
  assert(bridge_seek(&f,nullptr,0,0)==0&&bridge_read(&f,text,4)==4&&memcmp(text,"NEXU",4)==0);
  assert(bridge_write(&f,"x",1)<0);assert(bridge_close(&f)==0&&jobs.empty()&&fake::sd.reads+fake::usb.reads==calls);
  struct stat st{};assert(bridge_stat(guide.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>0);
  Dir* d{};assert(bridge_opendir(&d,FtpPath(use).c_str())==0);assert(std::string(bridge_readdir(&d,nullptr))==InstallGuideName);assert(bridge_dirlstat(&d,nullptr,nullptr,&st)==0&&S_ISREG(st.st_mode));assert(bridge_closedir(&d)==0);
 }
 // Failed, aborted and unplugged USB uploads never enqueue or fall back to SD.
 // A completed package or old .partial must not block another FTP install.
 for(auto use:{Use::InstallSd,Use::InstallUsb})for(int old=0;old<3;++old){
  Reset();auto a=std::make_shared<Area>(use,use==Use::InstallUsb?42:0);areas.push_back(a);
  auto& disk=use==Use::InstallUsb?fake::usb:fake::sd;
  const auto original=std::string(Base(use))+"/FIFA Street 2.nsp";
  const std::vector<u8> retained{'o','l','d'};
  if(old!=1)disk.files[original]=retained;
  if(old!=0)disk.files[original+".partial"]=retained;
  // The first numbered name is also occupied, so both entries must be checked.
  disk.files[RetryUploadPath(original,1)+".partial"]=retained;
  const auto before=disk.files;
  File* f{};assert(staged_open(&f,FtpPath(use,"FIFA Street 2.nsp").c_str(),FtpVfsOpenMode_WRITE)==0);
  assert(f->path==RetryUploadPath(original,2));
  assert(bridge_write(&f,"new-data",8)==8&&bridge_finish(&f)==0);
  assert(jobs.size()==1&&jobs.front().path==RetryUploadPath(original,2)&&jobs.front().usb==(use==Use::InstallUsb));
  for(const auto& [name,bytes]:before)assert(disk.files.at(name)==bytes);
  const auto live=activity::Get().Read();assert(live.errors==0&&live.uploaded==1&&live.queued==1);
  assert(std::any_of(live.lines.begin(),live.lines.end(),[](const auto& l){return l.text.find("Existing upload kept")!=l.text.npos;}));
 }
 // A second upload of the same name must not modify an active or queued file.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));File* first{};File* second{};
 const auto same=FtpPath(Use::InstallUsb,"Same.nsp");
 assert(staged_open(&first,same.c_str(),FtpVfsOpenMode_WRITE)==0);
 assert(bridge_write(&first,"one",3)==3);
 assert(staged_open(&second,same.c_str(),FtpVfsOpenMode_WRITE)==0&&first->path!=second->path);
 assert(bridge_write(&second,"two",3)==3);
 assert(bridge_finish(&second)==0&&bridge_finish(&first)==0&&jobs.size()==2);
 const auto queued=jobs.front().path;InstallJob installing;assert(TakeInstall(installing));
 File* third{};assert(staged_open(&third,same.c_str(),FtpVfsOpenMode_WRITE)==0&&third->path!=queued);
 assert(bridge_write(&third,"three",5)==5&&bridge_finish(&third)==0);FinishInstall(0);
 assert(fake::usb.files.at(queued)==std::vector<u8>({'t','w','o'}));
 // Simultaneous STOR opens serialize the USB check/create sequence, including
 // firmware where CreateFile would otherwise replace an existing partial.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));File* concurrent[2]{};
 std::thread upload_a([&]{assert(staged_open(&concurrent[0],same.c_str(),FtpVfsOpenMode_WRITE)==0);});
 std::thread upload_b([&]{assert(staged_open(&concurrent[1],same.c_str(),FtpVfsOpenMode_WRITE)==0);});
 upload_a.join();upload_b.join();assert(concurrent[0]->path!=concurrent[1]->path);
 for(auto& file:concurrent){assert(bridge_write(&file,"data",4)==4);assert(bridge_finish(&file)==0);}
 assert(jobs.size()==2&&fake::usb.files.size()==2&&fake::sd.files.empty());
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));fake::mounted=false;File* absent{};
 assert(staged_open(&absent,same.c_str(),FtpVfsOpenMode_WRITE)<0&&!absent);
 assert(fake::usb.creates==0&&fake::sd.creates==0&&activity::Get().Read().errors==1);
 // An interrupted retry keeps its new partial file, leaving the earlier one alone.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));
 const auto retryBase=std::string(Base(Use::InstallUsb))+"/Retry.nsp";
 fake::usb.files[retryBase+".partial"]={'o','l','d'};
 File* retry{};assert(staged_open(&retry,FtpPath(Use::InstallUsb,"Retry.nsp").c_str(),FtpVfsOpenMode_WRITE)==0);
 assert(bridge_write(&retry,"new",3)==3&&bridge_close(&retry)==0);
 assert(fake::usb.files.at(retryBase+".partial")==std::vector<u8>({'o','l','d'}));
 assert(fake::usb.files.contains(RetryUploadPath(retryBase,1)+".partial")&&jobs.empty());
 // Ordinary file/save transfer folders retain their existing no-overwrite policy.
 Reset();areas.push_back(std::make_shared<Area>(Use::FilesUsb,42));fake::usb.files[std::string(Root)+"/Keep.txt"]={'k'};
 File* ordinary{};assert(staged_open(&ordinary,FtpPath(Use::FilesUsb,"Keep.txt").c_str(),FtpVfsOpenMode_WRITE)<0&&!ordinary);
 assert(fake::usb.files.size()==1);
 // Exhausted candidates produce one clear error, not a rewrite or infinite loop.
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));
 const auto full=std::string(Base(Use::InstallUsb))+"/Full.nsp";
 for(unsigned n=0;n<100;++n)fake::usb.files[RetryUploadPath(full,n)]={'k'};
 File* blocked{};assert(staged_open(&blocked,FtpPath(Use::InstallUsb,"Full.nsp").c_str(),FtpVfsOpenMode_WRITE)<0&&!blocked);
 assert(fake::usb.files.size()==100&&activity::Get().Read().errors==1);
 const auto longPath=std::string(Base(Use::InstallUsb))+"/"+std::string(230,'a')+"\xe7\x95\x8c.NSP";
 const auto numbered=RetryUploadPath(longPath,99);const auto slash=numbered.find_last_of('/');
 assert(numbered.size()-slash-1<=240&&numbered.ends_with(" (upload 100).NSP"));
 assert(nexus::package::Supported(numbered));
 for(int failure=0;failure<5;++failure){Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,42));File* f{};assert(staged_open(&f,FtpPath(Use::InstallUsb,"abort.nsp").c_str(),FtpVfsOpenMode_WRITE)==0);
  if(failure==1)fake::fail_write=true;if(failure==2)fake::fail_flush=true;if(failure==3)fake::fail_rename=true;if(failure==4)++fake::generation;
  const auto wrote=bridge_write(&f,"hello",5);if(failure==1||failure==4)assert(wrote<0);else assert(wrote==5);
  if(failure==0)assert(bridge_close(&f)==0);else assert(bridge_finish(&f)<0);
  assert(!f&&jobs.empty()&&fake::sd.files.empty()&&fake::usb.files.contains(std::string(Base(Use::InstallUsb))+"/abort.nsp.partial"));
  const auto live=activity::Get().Read();assert(!live.uploaded&&!live.queued&&!live.installed&&live.errors==1);
 }
 // Cable/MTP uses the same upload owner with an announced size.
 for(bool complete:{false,true}){Reset();mode=Mode::Cable;Area sd(Use::InstallSd,0);const auto path=std::string(Base(Use::InstallSd))+"/Cable.nsp";
  assert(sd.Create(path,9)==0);File* file{};assert(sd.Open(path,true,9,&file)==0);assert(sd.Write(file,0,"test-data",9)==0);
  auto live=activity::Get().Read();assert(live.upload.expected==9&&live.upload.bytes==9&&live.installed==0);
  assert(sd.Close(file,complete)==0);live=activity::Get().Read();assert(live.uploaded==unsigned(complete)&&live.queued==unsigned(complete)&&live.errors==unsigned(!complete));
  if(complete){InstallJob job;assert(TakeInstall(job));FinishInstall(0x1759);live=activity::Get().Read();assert(!live.installed&&live.errors==1&&installFailed);assert(!TakeInstall(job));}
 }
 Reset();areas.push_back(std::make_shared<Area>(Use::InstallUsb,0));assert(areas[0]->Prepare()==0);File* f{};
 assert(staged_open(&f,FtpPath(Use::InstallUsb,"game.nsp").c_str(),FtpVfsOpenMode_WRITE)<0&&!f&&fake::sd.files.empty()&&fake::usb.files.empty());
 assert(staged_open(&f,FtpPath(Use::InstallUsb,InstallGuideName).c_str(),FtpVfsOpenMode_READ)==0);char text[4096]{};const auto got=bridge_read(&f,text,sizeof(text));assert(std::string(text,got).find("NOT READY")!=std::string::npos);assert(bridge_close(&f)==0);
 Dir* d{};assert(bridge_opendir(&d,FtpPath(Use::InstallUsb).c_str())==0);assert(bridge_readdir(&d,nullptr));assert(!bridge_readdir(&d,nullptr));assert(bridge_closedir(&d)==0);
 // Missing USB must be explained, never caused by an unopened NCM session.
 Reset();assert(ProbeUsb()==42&&fake::ncm_opens==1&&fake::ncm_closes==1&&!fake::ncm_refs&&usbProblem.empty());
 for(int failure=0;failure<5;++failure){Reset();if(failure==0)fake::ncm_error=71;if(failure==1)fake::firmware_error=72;if(failure==2)fake::mutation_error=73;if(failure==3)fake::query_error=74;if(failure==4)fake::mounted=false;
  assert(!ProbeUsb()&&!usbProblem.empty()&&!fake::ncm_refs&&fake::ncm_opens==1&&fake::ncm_closes==(failure?1:0));}
 assert(FtpNativePath("/3 - SD Card Files:/switch/test.nro")=="sdmc:/switch/test.nro");
 assert(FtpNativePath("5 - Export Installed Games:/Title/base.nsp")=="games:/Title/base.nsp");
 assert(FtpNativePath("3 - SD Card Files:bad")=="3 - SD Card Files:bad");
 assert(FtpNativePath("2 - Install to USB:/game.nsp")=="2 - Install to USB:/game.nsp");
 for(auto use:{Use::FilesSd,Use::FilesUsb,Use::InstallSd,Use::InstallUsb,Use::SaveDownloads,Use::SaveUploads})assert(strlen(VolumeName(use))+2<=32);
 assert(strlen(FtpGamesName)+2<=32);
 std::cout<<"PC transfer production tests: SD/USB routing, all package formats, virtual guide reads, immutable help, NCM lifetime, abort/error/unplug isolation and FTP aliases passed\n";
}
