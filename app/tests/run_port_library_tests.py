"""Exercise the actual combined-library scan worker with storage failures."""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
source=(app/'sphaira/source/ui/menus/port_manager_menu.cpp').read_text()
body=source.split('[selected_filter,found,detail](ProgressBox* p)->Result{',1)[1].split('},[this,found,detail]',1)[0]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
using Result=unsigned;
#define R_TRY(x) do{const auto rc=(x);if(rc)return rc;}while(0)
#define R_FAILED(x) ((x)!=0)
struct ProgressBox {bool cancel{};Result ShouldExitResult(){return cancel?99:0;}};
namespace f {std::string Fold(std::string x){for(auto& c:x)if(c>='A'&&c<='Z')c+=32;return x;}}
namespace nexusports {
struct Port {std::string name;bool usb;};
Result failures[2]{};unsigned scans[2]{};bool cancel_during{};
Result Scan(ProgressBox* p,bool usb,std::vector<Port>& result,std::string&){
 ++scans[usb];if(cancel_during)p->cancel=true;
 if(failures[usb])return failures[usb];result={{usb?"A port":"Z port",usb},{"Shared title",usb}};return 0;
}}
Result Run(unsigned selected_filter,ProgressBox* p,std::shared_ptr<std::vector<nexusports::Port>> found,std::shared_ptr<std::string> detail){
'''
suffix=r'''
}
int main(){
 for(unsigned filter=0;filter<3;++filter)for(unsigned failure=0;failure<4;++failure){
  nexusports::failures[0]=failure&1?10:0;nexusports::failures[1]=failure&2?20:0;
  nexusports::scans[0]=nexusports::scans[1]=0;ProgressBox p;
  auto result=std::make_shared<std::vector<nexusports::Port>>();auto detail=std::make_shared<std::string>();
  const auto rc=Run(filter,&p,result,detail);const bool sd=filter!=2,usb=filter!=1;
  assert(nexusports::scans[0]==sd&&nexusports::scans[1]==usb);
  const auto successful=(sd&&!(failure&1))+(usb&&!(failure&2));
  assert(result->size()==successful*2U&&bool(rc)==!successful);
  if(filter==0&&!failure){assert(result->at(0).name=="A port");assert(!result->at(1).usb&&result->at(2).usb);}
 }
 nexusports::failures[0]=nexusports::failures[1]=0;nexusports::cancel_during=true;ProgressBox p;
 auto result=std::make_shared<std::vector<nexusports::Port>>();auto detail=std::make_shared<std::string>();
 assert(Run(0,&p,result,detail)==99&&result->empty());
 std::cout<<"Production port scan: All/SD/USB, disconnected storage, partial success, duplicate labels and cancellation passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-port-grid-') as temp:
    unit=Path(temp)/'scan.cpp';exe=Path(temp)/'scan'
    unit.write_text(prefix+body+suffix)
    subprocess.run(['g++','-std=c++20','-O1','-UNDEBUG',str(unit),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
