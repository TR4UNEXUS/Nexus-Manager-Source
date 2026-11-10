// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "nexus/file_manager_core.hpp"
namespace fake {
struct State {int open_error{},stat_error{},seek_error{},read_error{},handle_error{},buffer_error{},buffer_errno{},mode{S_IFREG};size_t size{16},offset{},read_cap{SIZE_MAX},error_offset{SIZE_MAX};unsigned opens{},closes{},reads{},seeks{},handle_stats{},buffer_setups{};bool active{true},error{},unbuffered{};std::string path,open_mode;std::vector<size_t> read_sizes;} state;
int Stat(const char* path,struct stat* output){state.path=path;if(state.stat_error){errno=state.stat_error;return -1;}*output={};output->st_mode=state.mode;output->st_size=state.size;return 0;}
FILE* Open(const char* path,const char* mode){++state.opens;state.path=path;state.open_mode=mode;assert(state.open_mode=="rb");if(state.open_error){errno=state.open_error;return nullptr;}state.offset=0;state.error=false;state.unbuffered=false;return reinterpret_cast<FILE*>(&state);}
int SetBuffer(FILE* file,char* buffer,int mode,size_t size){assert(file==reinterpret_cast<FILE*>(&state)&&!buffer&&mode==_IONBF&&size==0);assert(state.offset==0&&!state.unbuffered);++state.buffer_setups;if(state.buffer_error){errno=state.buffer_errno;return state.buffer_error;}state.unbuffered=true;return 0;}
int Close(FILE*){++state.closes;return 0;}
int Seek(FILE*,off_t position,int origin){++state.seeks;assert(origin==SEEK_SET);if(state.seek_error){if(state.error_offset!=SIZE_MAX)state.offset=state.error_offset;errno=state.seek_error;return -1;}state.offset=position;return 0;}
size_t Read(void* output,size_t width,size_t count,FILE*){assert(state.unbuffered);++state.reads;state.read_sizes.push_back(count);assert(width==1);const auto remaining=state.offset<state.size?state.size-state.offset:0;const auto actual=std::min({count,remaining,state.read_cap});for(size_t i=0;i<actual;++i)static_cast<char*>(output)[i]='a'+(state.offset+i)%26;state.offset+=actual;if(state.read_error){state.error=true;if(state.error_offset!=SIZE_MAX)state.offset=state.error_offset;errno=state.read_error;}return actual;}
int Error(FILE*){return state.error;}
int Fileno(FILE*){return 42;}
int Fstat(int fd,struct stat* output){assert(fd==42);++state.handle_stats;if(state.handle_error){errno=state.handle_error;return -1;}*output={};output->st_mode=state.mode;output->st_size=state.size;output->st_ino=123;return 0;}
}
#define lstat fake::Stat
#define fopen fake::Open
#define fclose fake::Close
#define setvbuf fake::SetBuffer
#define fseeko fake::Seek
#define fread fake::Read
#define ferror fake::Error
#define fileno fake::Fileno
#define fstat fake::Fstat
#include "nexus/network_source.hpp"
#undef lstat
#undef fopen
#undef fclose
#undef setvbuf
#undef fseeko
#undef fread
#undef ferror
#undef fileno
#undef fstat
using Source=nexus::network::Source;
bool Active(){return fake::state.active;}
int main(){
    char data[32]{};std::uint64_t actual{};
    fake::state={};{Source source("nexusnet7:/",Active);assert(!source.Read("/game.nsp",3,data,5,actual));assert(actual==5&&std::string(data,5)=="defgh");assert(fake::state.path=="nexusnet7:/game.nsp");assert(!source.Read("/game.nsp",0,data,2,actual));assert(fake::state.opens==1);assert(!source.Read("/game.nsp",14,data,8,actual)&&actual==2);assert(!source.Read("/game.nsp",16,data,1,actual)&&actual==0);assert(!source.Read("/game.nsp",0,data,2,actual)&&actual==2);}assert(fake::state.closes==1);
    fake::state={};fake::state.seek_error=ETIMEDOUT;{Source source("nexusnet7:/",Active);assert(source.Read("/game.nsp",2,data,2,actual)==ETIMEDOUT&&actual==0);assert(fake::state.reads==0);}assert(fake::state.closes==1);
    fake::state={};fake::state.read_error=ECONNRESET;fake::state.read_cap=3;{Source source("nexusnet7:/",Active);assert(source.Read("/game.nsp",0,data,8,actual)==ECONNRESET&&actual==3);}assert(fake::state.closes==1);
    fake::state={};fake::state.open_error=EACCES;{Source source("nexusnet7:/",Active);assert(source.Read("/game.nsp",0,data,2,actual)==EACCES&&actual==0);}assert(fake::state.closes==0);
    // An unbuffered-mode setup failure cannot leave a usable pinned handle or
    // read any package bytes. EndRead/destruction must not close it twice.
    for(const int setup_errno:{0,ENOMEM}){fake::state={};fake::state.buffer_error=-1;fake::state.buffer_errno=setup_errno;
        {Source source("nexusnet7:/",Active);assert(source.BeginRead("/game.nsp")!=0);assert(fake::state.opens==1&&fake::state.buffer_setups==1&&fake::state.closes==1&&fake::state.reads==0);
         struct stat st{};assert(source.StatOpen(st)==EBADF);source.EndRead();assert(fake::state.closes==1);
         fake::state.buffer_error=0;assert(!source.BeginRead("/game.nsp")&&fake::state.opens==2&&fake::state.buffer_setups==2);
         assert(!source.Read("/game.nsp",0,data,4,actual)&&actual==4&&std::string(data,4)=="abcd");}
        assert(fake::state.closes==2);
    }
    fake::state={};fake::state.stat_error=EHOSTUNREACH;{Source source("nexusnet7:/",Active);assert(source.Read("/game.nsp",0,data,2,actual)==EHOSTUNREACH&&actual==0);}assert(fake::state.opens==0&&fake::state.closes==0);
    fake::state={};{Source source("nexusnet7:/",Active);assert(!source.Read("/game.nsp",0,data,1,actual));const auto reads=fake::state.reads,seeks=fake::state.seeks;fake::state.active=false;assert(source.Read("/game.nsp",1,data,1,actual)==ECANCELED&&actual==0);assert(fake::state.reads==reads&&fake::state.seeks==seeks);}assert(fake::state.closes==1);
    fake::state={};fake::state.mode=S_IFLNK;{Source source("nexusnet7:/",Active);assert(source.Read("/game.nsp",0,data,1,actual)==ENOTSUP);}assert(fake::state.opens==0);
    fake::state={};{Source source("nexusnet7:/",Active);assert(source.Read("/../game.nsp",0,data,1,actual)==EINVAL);assert(source.Read("/game.nsp",UINT64_MAX,data,1,actual)==EINVAL);assert(fake::state.opens==0);}
    fake::state={};{Source source("nexusnet7:/",Active);struct stat stat{};assert(source.StatOpen(stat)==EBADF&&fake::state.opens==0);
        assert(!source.BeginRead("/game.nsp"));assert(!source.BeginRead("/game.nsp")&&fake::state.opens==1&&fake::state.buffer_setups==1);assert(source.BeginRead("/other.nsp")==EBUSY&&fake::state.opens==1);
        assert(!source.StatOpen(stat)&&stat.st_ino==123&&stat.st_size==16);fake::state.handle_error=ESTALE;assert(source.StatOpen(stat)==ESTALE&&fake::state.opens==1&&fake::state.closes==0);
        const auto checks=fake::state.handle_stats;fake::state.active=false;assert(source.StatOpen(stat)==ECANCELED&&fake::state.handle_stats==checks);}
    assert(fake::state.closes==1&&fake::state.opens==1);
    // A complete multi-chunk download keeps the open handle's cursor. The old
    // implementation sought once per chunk; these contiguous reads need none.
    fake::state={};fake::state.size=2*1024*1024;{Source source("nexusnet7:/",Active);std::vector<char> chunk(256*1024);
        assert(!source.BeginRead("/game.nsz"));
        for(size_t offset=0;offset<fake::state.size;offset+=chunk.size()){
            assert(!source.Read("/game.nsz",offset,chunk.data(),chunk.size(),actual)&&actual==chunk.size());
            for(size_t i=0;i<chunk.size();++i)assert(chunk[i]=='a'+(offset+i)%26);
        }
        assert(fake::state.opens==1&&fake::state.reads==8&&fake::state.seeks==0);
        assert(!source.Read("/game.nsz",16,data,4,actual)&&std::string(data,actual)=="qrst"&&fake::state.seeks==1);
        assert(!source.Read("/game.nsz",20,data,4,actual)&&std::string(data,actual)=="uvwx"&&fake::state.seeks==1);
        assert(!source.Read("/game.nsz",0,data,4,actual)&&std::string(data,actual)=="abcd"&&fake::state.seeks==2);
    }assert(fake::state.closes==1);
    // The install adapter's larger 1 MiB payload remains one stdio request,
    // configured unbuffered, on the same handle. The newlib read-call split
    // comes from the toolchain audit; this tests the Source call boundary.
    fake::state={};fake::state.size=4*1024*1024;{Source source("nexusnet7:/",Active);std::vector<char> chunk(1024*1024);
        for(size_t offset=0;offset<fake::state.size;offset+=chunk.size()){
            assert(!source.Read("/game.nsp",offset,chunk.data(),chunk.size(),actual)&&actual==chunk.size());
            for(size_t i=0;i<chunk.size();++i)assert(chunk[i]=='a'+(offset+i)%26);
        }
        assert(fake::state.opens==1&&fake::state.buffer_setups==1&&fake::state.reads==4&&fake::state.seeks==0);
        assert(fake::state.read_sizes==std::vector<size_t>(4,1024*1024));
    }assert(fake::state.closes==1);
    // A zero-byte request must not move the cursor or force an open; a short
    // read advances by received bytes, not the requested amount.
    fake::state={};fake::state.read_cap=3;{Source source("nexusnet7:/",Active);
        assert(!source.Read("/game.xci",9,nullptr,0,actual)&&actual==0&&fake::state.opens==0);
        assert(source.Read("/game.xci",0,nullptr,1,actual)==EINVAL&&fake::state.opens==0);
        assert(!source.Read("/game.xci",0,data,8,actual)&&actual==3&&std::string(data,actual)=="abc");
        assert(!source.Read("/game.xci",12,nullptr,0,actual)&&actual==0&&fake::state.seeks==0);
        assert(!source.Read("/game.xci",3,data,8,actual)&&actual==3&&std::string(data,actual)=="def"&&fake::state.seeks==0);
        assert(!source.Read("/game.xci",14,data,8,actual)&&actual==2&&std::string(data,actual)=="op"&&fake::state.seeks==1);
        assert(!source.Read("/game.xci",16,data,8,actual)&&actual==0&&fake::state.seeks==1);
        assert(!source.Read("/game.xci",0,data,2,actual)&&actual==2&&std::string(data,actual)=="ab"&&fake::state.seeks==2);
    }
    // A failed seek may leave the underlying position indeterminate. Even a
    // retry at the previously known position must seek again.
    fake::state={};{Source source("nexusnet7:/",Active);assert(!source.BeginRead("/game.xcz"));
        fake::state.seek_error=ETIMEDOUT;fake::state.error_offset=12;
        assert(source.Read("/game.xcz",8,data,2,actual)==ETIMEDOUT&&actual==0);
        fake::state.seek_error=0;
        assert(!source.Read("/game.xcz",0,data,2,actual)&&std::string(data,actual)=="ab"&&fake::state.seeks==2);
    }
    // Positive bytes plus an error are returned as failure and invalidate the
    // cursor. This is never treated as successful read-ahead by a retry.
    fake::state={};{Source source("nexusnet7:/",Active);fake::state.read_error=EIO;fake::state.read_cap=3;fake::state.error_offset=12;
        assert(source.Read("/game.nsp",0,data,8,actual)==EIO&&actual==3&&fake::state.seeks==0);
        fake::state.read_error=0;fake::state.error=false;
        assert(!source.Read("/game.nsp",3,data,3,actual)&&std::string(data,actual)=="def"&&fake::state.seeks==1);
        source.EndRead();
        assert(!source.Read("/other.nsp",0,data,3,actual)&&std::string(data,actual)=="abc"&&fake::state.opens==2&&fake::state.seeks==1);
    }assert(fake::state.closes==2);
    std::cout<<"Production network Source: unbuffered setup/failure cleanup, one stdio request per 1 MiB chunk, sequential cursor, random access, short reads, EOF/errors, cancellation and pinned handle cleanup passed\n";
}
