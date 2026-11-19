from pathlib import Path
import re,os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
strip=lambda s:re.sub(r'^#(?:include|pragma)[^\n]*\n','',s,flags=re.M)
stream=strip((root/'sphaira/include/yati/source/stream.hpp').read_text())
stream_cpp=strip((root/'sphaira/source/yati/source/stream.cpp').read_text())
adapter=(root/'sphaira/source/nexus/transfer_install.cpp').read_text();a=adapter.index('class FtpSource final:');b=adapter.index('\nResult InstallStream(',a)
fixture=r"""
#include "nexus/ftp_stream.hpp"
#include <atomic>
#include <cassert>
#include <future>
#include <memory>
#include <iostream>
using u8=uint8_t;using u64=uint64_t;using s64=int64_t;using Result=uint32_t;
constexpr Result Result_StreamBadSeek=501,Result_TransferCancelled=502;
#define R_TRY(x) do{const auto rc_=(x);if(rc_)return rc_;}while(false)
#define R_UNLESS(c,r) do{if(!(c))return r;}while(false)
#define R_SUCCEED() return 0
#define R_FAILED(rc) ((rc)!=0)
namespace sphaira::ui{struct ProgressBox{std::atomic_bool cancel{};Result ShouldExitResult(){return cancel?81:0;}void NewTransfer(const char*){}};}
namespace sphaira::yati::source{struct Base{virtual ~Base()=default;virtual Result Read(void*,s64,s64,u64*)=0;virtual void SignalCancel(){};virtual Result FinishRead(s64){return 0;}virtual bool IsStream()const{return false;}};}
namespace nexus::lifecycle{bool CanPoll(){return true;}}
"""+stream+stream_cpp+'\nnamespace nexus::transfer {\n'+adapter[a:b]+r"""
}
int main(){
 using namespace nexus::transfer;
 for(int fail=0;fail<4;++fail){
  auto pipe=std::make_shared<FtpStream>(64);sphaira::ui::ProgressBox box;FtpSource source(pipe,&box);assert(source.IsStream());
  std::vector<u8> bytes(65536);for(size_t i=0;i<bytes.size();++i)bytes[i]=i%251;
  auto writer=std::async(std::launch::async,[&]{const auto rc=pipe->Write(bytes.data(),0,bytes.size());pipe->EndInput(fail!=2);return rc;});
  u8 first[32];u64 got{};assert(!source.Read(first,0,sizeof(first),&got)&&got==32&&!memcmp(first,bytes.data(),32));
  assert(source.Read(first,0,1,&got)==Result_StreamBadSeek);
  // Forward skips discard padding without buffering a package.
  u8 next[32];assert(!source.Read(next,8192,sizeof(next),&got)&&got==32&&!memcmp(next,bytes.data()+8192,32));
  const auto rc=source.FinishRead(fail==1?bytes.size()+1:bytes.size());
  assert(writer.get()==0);
  if(fail==1||fail==2)assert(rc);else assert(!rc);
  pipe->Complete(rc);assert(bool(pipe->WaitResult())==bool(rc));
 }
 // Cancellation must release a read stalled waiting for PC bytes.
 auto pipe=std::make_shared<FtpStream>(64);sphaira::ui::ProgressBox box;FtpSource source(pipe,&box);
 auto reader=std::async(std::launch::async,[&]{u8 data;u64 got{};return source.Read(&data,0,1,&got);});
 box.cancel=true;assert(reader.get()==81);assert(pipe->Write("a",0,1)!=0);
 auto failed=std::make_shared<FtpStream>(64);FtpSource stopped(failed,&box);stopped.SignalCancel();assert(failed->WaitResult()!=0);
 std::cout<<"Actual FTP source: forward-only reads, padding discard, EOF minimum length, truncated/aborted transport and cancellation passed\n";
}
"""
with tempfile.TemporaryDirectory(prefix='nexus-ftp-source-') as tmp:
 cpp=Path(tmp)/'test.cpp';cpp.write_text(fixture)
 for opt in ('-O0','-O2'):
  exe=Path(tmp)/('test'+opt)
  subprocess.run(['g++','-std=c++20',opt,'-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(root/'sphaira/include'),str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
