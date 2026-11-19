"""Check actual raw SD MTP/FTP entry points cannot replace installer staging."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
haze=(root/'sphaira/source/haze_helper.cpp').read_text(encoding='utf-8')
haze=haze[haze.index('struct FsProxy final'):haze.index('struct FsProxyVfs')]
ftp=(root/'sphaira/source/ftpsrv_helper.cpp').read_text(encoding='utf-8')
def block(source,marker):
 start=source.index(marker);end=source.index('{',start)+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
methods='\n'.join(block(haze,sig) for sig in ['bool CanMutate(', 'Result CreateFile(', 'Result DeleteFile(', 'Result RenameFile(', 'Result OpenFile(', 'Result CreateDirectory(', 'Result DeleteDirectoryRecursively(', 'Result RenameDirectory('])
wrappers='\n'.join(block(ftp,'int manager_sd_'+name+'(') for name in ['open','mkdir','unlink','rmdir','rename'])
fixture=r'''
#include "nexus/transfer_policy.hpp"
#include <memory>
#include <string>
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <iostream>
using Result=uint32_t;using u32=uint32_t;using s64=int64_t;
#define R_UNLESS(c,r) do{if(!(c))return r;}while(false)
#define R_SUCCEED() return 0
#define R_FAILED(x) ((x)!=0)
#define MAKERESULT(a,b) 99
#define log_write(...) ((void)0)
constexpr int FsOpenMode_Read=1,FsOpenMode_Write=2,FsOpenMode_Append=4;
namespace haze {enum FileOpenMode{FileOpenMode_READ,FileOpenMode_WRITE};struct File{void* impl{};};}
struct File{};
struct Backend {unsigned calls{};
 Result CreateFile(const std::string&,int,int){++calls;return 0;}
 Result DeleteFile(const std::string&){++calls;return 0;}
 Result RenameFile(const std::string&,const std::string&){++calls;return 0;}
 Result OpenFile(const std::string&,u32,File*){++calls;return 0;}
 Result CreateDirectory(const std::string&){++calls;return 0;}
 Result DeleteDirectoryRecursively(const std::string&){++calls;return 0;}
 Result RenameDirectory(const std::string&,const std::string&){++calls;return 0;}
};
struct FsProxy {
 bool m_protect_install_cache=true;Backend* m_fs;
 std::string FixPath(const char* path)const{return std::string("/")+path;}
 // METHODS
};
enum FtpVfsOpenMode{FtpVfsOpenMode_READ,FtpVfsOpenMode_WRITE,FtpVfsOpenMode_APPEND};
struct FtpBackend {unsigned calls{};
 int open(void*,const std::string&,FtpVfsOpenMode){++calls;return 0;}
 int mkdir(const std::string&){++calls;return 0;}
 int unlink(const std::string&){++calls;return 0;}
 int rmdir(const std::string&){++calls;return 0;}
 int rename(const std::string&,const std::string&){++calls;return 0;}
}g_vfs_fs;
std::string vfs_stdio_fix_path(const char* p){return nexus::transfer::FtpNativePath(p);}
// WRAPPERS
int main(){
 Backend native;FsProxy proxy{true,&native};
 const char* ordinary="/games/keep.nsp";
 for(const char* path:{"/switch/nexusmanager/transfers/install/game.nsp","/SWITCH/NEXUSMANAGER/TRANSFERS/INSTALL/game.NSP","/switch/nexusm~1/transfers/install/game.nsp","/switch/nexusmanager/transfers","/switch","/switch/nexusmanager/transfers/install/game.nsp.partial"}){
  haze::File file;assert(proxy.CreateFile(path,1));assert(proxy.DeleteFile(path));assert(proxy.CreateDirectory(path));assert(proxy.DeleteDirectoryRecursively(path));
  assert(proxy.RenameFile(path,ordinary));assert(proxy.RenameFile(ordinary,path));assert(proxy.RenameDirectory(path,ordinary));assert(proxy.RenameDirectory(ordinary,path));
  assert(proxy.OpenFile(path,haze::FileOpenMode_WRITE,&file));assert(!native.calls);
  const auto ftp="sdmc:"+std::string(path);assert(manager_sd_open(nullptr,ftp.c_str(),FtpVfsOpenMode_WRITE)<0);assert(manager_sd_unlink(ftp.c_str())<0);assert(manager_sd_mkdir(ftp.c_str())<0);assert(manager_sd_rmdir(ftp.c_str())<0);
  assert(manager_sd_rename(ftp.c_str(),"sdmc:/games/file.nsp")<0);assert(manager_sd_rename("sdmc:/games/file.nsp",ftp.c_str())<0);assert(!g_vfs_fs.calls);
 }
 haze::File file;assert(!proxy.OpenFile("/switch/nexusmanager/transfers/install/old.nsp",haze::FileOpenMode_READ,&file));delete static_cast<File*>(file.impl);
 assert(!proxy.CreateFile(ordinary,1));assert(native.calls==2);
 assert(!manager_sd_open(nullptr,"3 - SD Card Files:/games/file.nsp",FtpVfsOpenMode_WRITE));assert(g_vfs_fs.calls==1);
 proxy.m_protect_install_cache=false;assert(!proxy.DeleteFile("/switch/nexusmanager/transfers/install/old.nsp"));
 std::cout<<"Raw SD MTP and FTP guards: writes, deletes, renames, parents and FAT aliases blocked only for installer staging; reads and ordinary transfers available\n";
}
'''.replace('// METHODS',methods.replace(' override','')).replace('// WRAPPERS',wrappers)
with tempfile.TemporaryDirectory(prefix='nexus-transfer-guard-') as directory:
 cpp=Path(directory)/'actual.cpp';cpp.write_text(fixture,encoding='utf-8')
 binary=Path(directory)/'test'
 subprocess.run(['g++','-std=c++20','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-no-pie','-I',str(root/'sphaira/include'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,timeout=30)
