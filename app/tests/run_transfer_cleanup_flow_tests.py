"""Run the actual transfer installer worker and its completion callback."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'sphaira/source/nexus/transfer_install.cpp').read_text(encoding='utf-8')
body=s[s.index('void ShowNextInstall(){'):s.rfind('\n}')]
fixture=r'''
#include "nexus/transfer_policy.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <cassert>
#include <iostream>
using u64=uint64_t;using Result=uint32_t;
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(false)
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
namespace fake {Result install{},cleanup{},cancel{},finished{};size_t installed=1,skipped=0;unsigned deleted{},notified{},errors{},calls{};bool queued=true,source_closed{},cancel_after{};}
namespace fs{using FsPath=std::string;}
namespace sphaira {
namespace yati {struct InstallOutcome{size_t installed_records{},skipped_records{};};}
namespace ui {struct ProgressBox{Result ShouldExitResult(){return fake::cancel;}void NewTransfer(const char*){};};enum class ProgressBoxOption{TransferLog};}
struct App {
 template<class Box,class Work,class Done>static void Push(int,const char*,const std::string&,Work work,Done done,ui::ProgressBoxOption){Box p;done(work(&p));}
 static void Notify(const char*){++fake::notified;}
 static void PushErrorBox(Result,const char*){++fake::errors;}
};
}
namespace nexus {
enum class Target{Sd,Usb};namespace install{struct Diagnostic{};}
void Log(const char*,Result,u64){}
namespace transfer {
struct FtpStream {static constexpr Result Aborted=90;Result result=99;void Complete(Result rc){result=rc;}};
struct InstallJob{bool usb{};u64 generation{},size{};std::string path;std::shared_ptr<FtpStream> stream;u64 upload_id{};};
InstallJob next;
std::atomic<sphaira::ui::ProgressBox*> progress{};
bool TakeInstall(InstallJob& out){if(!fake::queued)return false;fake::queued=false;out=next;return true;}
void FinishInstall(Result rc){fake::finished=rc;}
Result RemoveInstalledUpload(const InstallJob&){assert(fake::source_closed);++fake::deleted;return fake::cleanup;}
Result InstallStream(sphaira::ui::ProgressBox*,const InstallJob&,install::Diagnostic&){return fake::install;}
namespace activity{struct Journal{void Event(const std::string&){};void Failure(const char*,Result){}};Journal& Get(){static Journal j;return j;}std::string Name(const std::string& s){return s;}}
}}
namespace sphaira::nexusapp {
Result Install(ui::ProgressBox*,bool,u64,const std::string&,nexus::Target,u64,nexus::install::Diagnostic*,yati::InstallOutcome* out){
 ++fake::calls;out->installed_records=fake::installed;out->skipped_records=fake::skipped;
 fake::source_closed=true;if(fake::cancel_after)fake::cancel=88;return fake::install;
}
}
namespace nexus::transfer {
// PRODUCTION
}
int main(){using namespace nexus::transfer;
 for(int test=0;test<9;++test){
  fake::install=fake::cleanup=fake::cancel=0;fake::finished=999;fake::installed=1;fake::skipped=0;
  fake::deleted=fake::notified=fake::errors=fake::calls=0;fake::queued=true;fake::source_closed=fake::cancel_after=false;
  next={false,0,7,"/switch/nexusmanager/transfers/install/game.nsp",{},7};
  if(test==1)fake::install=44;
  if(test==2)fake::skipped=1;
  if(test==3)fake::installed=0;
  if(test==4)fake::cleanup=55;
  if(test==5)fake::cancel=88;
  if(test==6)fake::cancel_after=true;
  if(test==7||test==8)next.stream=std::make_shared<FtpStream>();
  if(test==8)fake::install=44;
  ShowNextInstall();assert(!progress.load());
  assert(fake::deleted==unsigned(test==0||test==4));
  assert(fake::finished==(test==5?88:fake::install));
  assert(fake::errors==unsigned(test==1||test==5||test==8));
  assert(fake::notified==unsigned(test==4||test==6));
  if(next.stream)assert(next.stream->result==fake::install);
 }
 std::cout<<"Actual install completion: cleanup only after full success and closed source, skipped/failed/cancelled packages retained, cleanup warning nonfatal, FTP acknowledgement preserved\n";
}
'''.replace('// PRODUCTION',body)
with tempfile.TemporaryDirectory(prefix='nexus-cleanup-flow-') as tmp:
 cpp=Path(tmp)/'actual.cpp';cpp.write_text(fixture,encoding='utf-8')
 for mode in ('-O0','-O2'):
  binary=Path(tmp)/('test'+mode)
  subprocess.run(['g++','-std=c++20',mode,'-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie','-I',str(root/'sphaira/include'),str(cpp),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True,timeout=30)
