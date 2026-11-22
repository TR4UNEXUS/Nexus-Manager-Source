// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/bootstrap.h"
#include "nexus/nx_Client.hpp"
#include "defines.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace nexus::support {
using Bytes=std::vector<u8>;
constexpr Result Missing=MAKERESULT(2,1);
struct Path{char value[FS_MAX_PATH]{};explicit Path(const std::string& s){snprintf(value,sizeof(value),"%s",s.c_str());}};
class Storage {
    FsFileSystem sd{};bool opened{};
public:
    ~Storage(){if(opened)fsFsClose(&sd);}
    Result Open(){
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){Client c;NexusVsdStatus s{};R_TRY(c.VirtualSdStatus(s));R_UNLESS(!(s.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),Unavailable);}
        R_TRY(fsOpenSdCardFileSystem(&sd));opened=true;return 0;
    }
    Result Read(const std::string& name,size_t limit,Bytes& bytes){
        bytes.clear();Path path(name);FsFile f{};R_TRY(fsFsOpenFile(&sd,path.value,FsOpenMode_Read,&f));ON_SCOPE_EXIT(fsFileClose(&f));
        s64 size{};R_TRY(fsFileGetSize(&f,&size));R_UNLESS(size>=0&&u64(size)<=limit,Invalid);bytes.resize(size);u64 got{};
        R_TRY(fsFileRead(&f,0,bytes.data(),bytes.size(),FsReadOption_None,&got));R_UNLESS(got==bytes.size(),Invalid);return 0;
    }
    Result Directory(const std::string& name){Path p(name);FsDirEntryType t{};auto rc=fsFsGetEntryType(&sd,p.value,&t);if(rc==Missing)return fsFsCreateDirectory(&sd,p.value);R_TRY(rc);R_UNLESS(t==FsDirEntryType_Dir,Invalid);return 0;}
    Result Remove(const std::string& name){Path p(name);auto rc=fsFsDeleteFile(&sd,p.value);R_UNLESS(!rc||rc==Missing,rc);return fsFsCommit(&sd);}
    Result List(const std::string& name,std::vector<std::string>& names){
        names.clear();Path p(name);FsDir dir{};auto rc=fsFsOpenDirectory(&sd,p.value,FsDirOpenMode_ReadFiles,&dir);if(rc==Missing)return 0;R_TRY(rc);ON_SCOPE_EXIT(fsDirClose(&dir));
        for(;;){std::array<FsDirectoryEntry,8> entries{};s64 n{};R_TRY(fsDirRead(&dir,&n,entries.size(),entries.data()));R_UNLESS(n>=0&&n<=s64(entries.size()),Invalid);if(!n)return 0;
            for(s64 i=0;i<n;++i){R_UNLESS(memchr(entries[i].name,0,sizeof(entries[i].name)),Invalid);names.emplace_back(entries[i].name);R_UNLESS(names.size()<=4096,Invalid);}}
    }
    // Callers keep a separate valid revision. Only the app-owned target and its
    // staging file are replaced; no routing indexes or user game paths are used.
    Result Write(const std::string& name,const Bytes& bytes){
        Path target(name),temp(name+".tmp");auto rc=fsFsDeleteFile(&sd,temp.value);R_UNLESS(!rc||rc==Missing,rc);
        R_TRY(fsFsCreateFile(&sd,temp.value,bytes.size(),0));FsFile file{};R_TRY(fsFsOpenFile(&sd,temp.value,FsOpenMode_Write,&file));
        rc=fsFileWrite(&file,0,bytes.data(),bytes.size(),FsWriteOption_Flush);fsFileClose(&file);R_TRY(rc);R_TRY(fsFsCommit(&sd));
        Bytes check;R_TRY(Read(name+".tmp",bytes.size(),check));R_UNLESS(check==bytes,Invalid);
        rc=fsFsDeleteFile(&sd,target.value);R_UNLESS(!rc||rc==Missing,rc);R_TRY(fsFsRenameFile(&sd,temp.value,target.value));R_TRY(fsFsCommit(&sd));
        R_TRY(Read(name,bytes.size(),check));R_UNLESS(check==bytes,Invalid);return 0;
    }
};
inline Result Folders(Storage& s,const char* leaf){R_TRY(s.Directory("/switch"));R_TRY(s.Directory("/switch/nexusmanager"));return s.Directory(std::string("/switch/nexusmanager/")+leaf);}
}
