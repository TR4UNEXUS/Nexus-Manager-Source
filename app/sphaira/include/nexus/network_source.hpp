// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/file_manager_core.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <sys/stat.h>
namespace nexus::network {
// Read-only view of a previously connected mount. Paths are relative to the
// share; no URL or credential is accepted here. The same code is host-tested.
class Source {
    std::string root,input_path;
    FILE* input{};
    std::uint64_t input_position{};
    bool input_position_valid{};
    std::function<bool()> check;
    std::string Path(const std::string& path)const{return root+path.substr(1);}
public:
    Source(std::string mount,std::function<bool()> valid):root(std::move(mount)),check(std::move(valid)){}
    ~Source(){EndRead();}
    Source(const Source&)=delete;
    Source& operator=(const Source&)=delete;
    void EndRead(){if(input)fclose(input);input=nullptr;input_path.clear();input_position=0;input_position_valid=false;}
    // Pin one read-only handle. Installers use this once and never retarget it
    // after a disconnect or a replacement file appearing at the same path.
    int BeginRead(const std::string& path){
        if(!files::Canonical(path))return EINVAL;if(!check())return ECANCELED;
        if(input)return input_path==path?0:EBUSY;
        files::Entry file;bool exists{};if(auto error=Stat(path,file,exists))return error;if(!exists)return ENOENT;if(file.directory)return EISDIR;
        input=fopen(Path(path).c_str(),"rb");if(!input)return errno?errno:EIO;
        // Newlib otherwise splits large fread calls into a bulk read and a
        // small buffer refill. On SMB/NFS that is another synchronous network
        // round trip. The installer already supplies its own bounded buffers.
        if(setvbuf(input,nullptr,_IONBF,0)){
            const auto error=errno?errno:EIO;EndRead();return error;
        }
        input_path=path;input_position=0;input_position_valid=true;return 0;
    }
    // Inspect the actual opened handle, never reopen it or substitute a new
    // path. Errors leave the handle owned until the operation is destroyed.
    int StatOpen(struct stat& out){
        out={};if(!input)return EBADF;if(!check())return ECANCELED;
        if(fstat(fileno(input),&out))return errno?errno:EIO;
        return 0;
    }
    int Stat(const std::string& path,files::Entry& entry,bool& exists){
        entry={};exists=false;if(!files::Canonical(path))return EINVAL;if(!check())return ECANCELED;
        struct stat st{};
        if(lstat(Path(path).c_str(),&st)){if(errno==ENOENT)return 0;return errno?errno:EIO;}
        if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))return ENOTSUP;
        if(st.st_size<0)return EOVERFLOW;
        entry.path=path;entry.directory=S_ISDIR(st.st_mode);entry.size=entry.directory?0:st.st_size;exists=true;return 0;
    }
    int List(const std::string& path,std::vector<std::string>& names){
        names.clear();if(!files::Canonical(path))return EINVAL;if(!check())return ECANCELED;
        files::Entry folder;bool exists{};if(auto error=Stat(path,folder,exists))return error;if(!exists)return ENOENT;if(!folder.directory)return ENOTDIR;
        DIR* dir=opendir(Path(path).c_str());if(!dir)return errno?errno:EIO;
        struct Close{DIR* directory;~Close(){closedir(directory);}} close{dir};
        for(;;){
            if(!check())return ECANCELED;
            errno=0;auto* entry=readdir(dir);
            // libnx's devoptab uses ENOENT as its end-of-directory sentinel.
            if(!entry)return !errno||errno==ENOENT?0:errno;
            if(!memchr(entry->d_name,0,sizeof(entry->d_name)))return EINVAL;
            if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
            if(names.size()>=files::MaxEntries)return EOVERFLOW;
            names.emplace_back(entry->d_name);
        }
    }
    int Read(const std::string& path,std::uint64_t offset,void* data,size_t count,std::uint64_t& actual){
        actual=0;
        if(!files::Canonical(path)||offset>files::MaxBytes||count>files::MaxBytes-offset||(count&&!data))return EINVAL;
        if(!check())return ECANCELED;
        if(!count)return 0;
        if(!input||input_path!=path){
            EndRead();if(auto error=BeginRead(path))return error;
        }
        // Keep the logical stdio cursor through sequential streaming chunks.
        // Repeated fseeko calls discard useful read-ahead on some stdio paths;
        // metadata/random access still seeks on this same pinned handle.
        if(!input_position_valid||input_position!=offset){
            input_position_valid=false;
            if(fseeko(input,offset,SEEK_SET))return errno?errno:EIO;
            input_position=offset;input_position_valid=true;
        }
        actual=fread(data,1,count,input);
        if(ferror(input)){input_position_valid=false;return errno?errno:EIO;}
        if(actual>count){input_position_valid=false;actual=0;return EIO;}
        input_position+=actual;
        return 0;
    }
};
}
