// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/move_checkpoint.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/bootstrap.h"
#include "defines.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <set>
namespace sphaira::nexusapp {
namespace {
constexpr size_t Limit=8*1024*1024;
constexpr char Root[]="/switch/nexusmanager/move-checkpoints";
constexpr Result Missing=MAKERESULT(2,1);
struct Path {char text[FS_MAX_PATH]{};explicit Path(const std::string& s){snprintf(text,sizeof(text),"%s",s.c_str());}};
struct Header {u32 magic=0x314d584e,version=1;u64 application{},sequence{};u32 source{},destination{},records{},files{};};
static_assert(sizeof(Header)==40 && sizeof(NcmContentMetaKey)==16 && sizeof(NcmContentInfo)==24);
using Bytes=std::vector<u8>;
template<class T> void Put(Bytes& b,const T& value){const auto p=reinterpret_cast<const u8*>(&value);b.insert(b.end(),p,p+sizeof(value));}
bool Encode(const MoveCheckpoint& c,Bytes& b){
    if(!c.application || !c.sequence || !nexus::ValidMove(c.source,c.destination) || c.records.empty() || c.records.size()>4096 || c.files.empty() || c.files.size()>65536)return false;
    b.clear();Put(b,Header{0x314d584e,1,c.application,c.sequence,u32(c.source),u32(c.destination),u32(c.records.size()),u32(c.files.size())});
    for(const auto& r:c.records){if(r.raw.size()<8 || r.raw.size()>1024*1024 || b.size()+20+r.raw.size()>Limit)return false;Put(b,r.key);Put(b,u32(r.raw.size()));b.insert(b.end(),r.raw.begin(),r.raw.end());}
    for(const auto& f:c.files){Put(b,f.info);Put(b,f.hash);}
    if(b.size()+32>Limit)return false;std::array<u8,32> hash{};sha256CalculateHash(hash.data(),b.data(),b.size());Put(b,hash);return true;
}
bool Decode(const Bytes& b,MoveCheckpoint& c){
    c={};if(b.size()<sizeof(Header)+32 || b.size()>Limit)return false;
    std::array<u8,32> hash{};sha256CalculateHash(hash.data(),b.data(),b.size()-32);
    if(memcmp(hash.data(),b.data()+b.size()-32,32))return false;
    size_t off{};auto get=[&](auto& value){if(sizeof(value)>b.size()-32-off)return false;memcpy(&value,b.data()+off,sizeof(value));off+=sizeof(value);return true;};
    Header h{};if(!get(h) || h.magic!=0x314d584e || h.version!=1 || !h.application || !h.sequence || h.source>2 || h.destination>2 || !h.records || h.records>4096 || !h.files || h.files>65536)return false;
    c.application=h.application;c.sequence=h.sequence;c.source=nexus::Target(h.source);c.destination=nexus::Target(h.destination);
    if(!nexus::ValidMove(c.source,c.destination))return false;
    std::set<std::array<u8,16>> keys,ids;
    for(u32 i=0;i<h.records;++i){MoveCheckpointRecord r;u32 size{};if(!get(r.key)||!get(size)||size<8||size>1024*1024||size>b.size()-32-off)return false;
        std::array<u8,16> key{};memcpy(key.data(),&r.key,16);if(!keys.insert(key).second)return false;
        r.raw.assign(b.begin()+off,b.begin()+off+size);off+=size;c.records.push_back(std::move(r));}
    for(u32 i=0;i<h.files;++i){MoveCheckpointFile f;if(!get(f.info)||!get(f.hash))return false;u64 size{};ncmContentInfoSizeToU64(&f.info,&size);
        std::array<u8,16> id{};memcpy(id.data(),&f.info.content_id,16);if(!size || size>INT64_MAX || id==std::array<u8,16>{} || !ids.insert(id).second)return false;
        c.files.push_back(f);}
    return off==b.size()-32;
}
std::string Name(const MoveCheckpoint& c){char out[100]{};snprintf(out,sizeof(out),"%s/%016llx-%016llx.move",Root,(unsigned long long)c.application,(unsigned long long)c.sequence);return out;}
struct Disk {
    FsFileSystem sd{};
    ~Disk(){if(serviceIsActive(&sd.s))fsFsClose(&sd);}
    Result Open(){
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){nexus::Client c;NexusVsdStatus v{};R_TRY(c.VirtualSdStatus(v));R_UNLESS(!(v.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Unavailable);}
        R_TRY(fsOpenSdCardFileSystem(&sd));
        for(const char* name:{"/switch","/switch/nexusmanager",Root}){Path p(name);FsDirEntryType type{};auto rc=fsFsGetEntryType(&sd,p.text,&type);if(rc==Missing){R_TRY(fsFsCreateDirectory(&sd,p.text));}else {R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir,nexus::Invalid);}}
        return 0;
    }
    Result Read(const std::string& name,Bytes& b){Path p(name);FsFile f{};R_TRY(fsFsOpenFile(&sd,p.text,FsOpenMode_Read,&f));ON_SCOPE_EXIT(fsFileClose(&f));
        s64 size{};R_TRY(fsFileGetSize(&f,&size));R_UNLESS(size>=0&&size<=s64(Limit),nexus::Invalid);b.resize(size);u64 got{};R_TRY(fsFileRead(&f,0,b.data(),b.size(),FsReadOption_None,&got));R_UNLESS(got==b.size(),nexus::Invalid);return 0;}
};
}
Result PendingMoves(std::vector<MoveCheckpoint>& out){
    out.clear();size_t loaded_bytes{};Disk d;R_TRY(d.Open());FsDir dir{};Path root(Root);R_TRY(fsFsOpenDirectory(&d.sd,root.text,FsDirOpenMode_ReadFiles,&dir));ON_SCOPE_EXIT(fsDirClose(&dir));
    for(;;){std::array<FsDirectoryEntry,8> page{};s64 count{};R_TRY(fsDirRead(&dir,&count,page.size(),page.data()));R_UNLESS(count>=0&&count<=s64(page.size()),nexus::Invalid);if(!count)break;
        for(s64 i=0;i<count;++i){R_UNLESS(memchr(page[i].name,0,sizeof(page[i].name)),nexus::Invalid);const std::string name=page[i].name;
            if(!name.ends_with(".move"))continue;R_UNLESS(out.size()<256,nexus::Invalid);Bytes b;R_TRY(d.Read(std::string(Root)+"/"+name,b));MoveCheckpoint c;loaded_bytes+=b.size();R_UNLESS(loaded_bytes<=32*1024*1024,nexus::Invalid);
            R_UNLESS(Decode(b,c)&&Name(c)==std::string(Root)+"/"+name,nexus::Invalid);out.push_back(std::move(c));}}
    return 0;
}
Result SaveMoveCheckpoint(MoveCheckpoint& c){
    std::vector<MoveCheckpoint> pending;R_TRY(PendingMoves(pending));
    R_UNLESS(std::none_of(pending.begin(),pending.end(),[&](const auto& r){return r.application==c.application;}),nexus::Invalid);
    c.sequence=armGetSystemTick();Bytes bytes;R_UNLESS(Encode(c,bytes),nexus::Invalid);Disk d;R_TRY(d.Open());Path temp(Name(c)+".tmp"),final(Name(c));
    R_TRY(fsFsCreateFile(&d.sd,temp.text,bytes.size(),0));FsFile file{};R_TRY(fsFsOpenFile(&d.sd,temp.text,FsOpenMode_Write,&file));
    auto rc=fsFileWrite(&file,0,bytes.data(),bytes.size(),FsWriteOption_Flush);fsFileClose(&file);R_TRY(rc);R_TRY(fsFsCommit(&d.sd));
    Bytes actual;R_TRY(d.Read(temp.text,actual));R_UNLESS(actual==bytes,nexus::Invalid);
    R_TRY(fsFsRenameFile(&d.sd,temp.text,final.text));R_TRY(fsFsCommit(&d.sd));R_TRY(d.Read(final.text,actual));R_UNLESS(actual==bytes,nexus::Invalid);
    nexus::Log("LR1-move-checkpoint-saved",0,c.application);return 0;
}
Result CompleteMoveCheckpoint(const MoveCheckpoint& c){
    Disk d;R_TRY(d.Open());Bytes before,expected;R_UNLESS(Encode(c,expected),nexus::Invalid);R_TRY(d.Read(Name(c),before));R_UNLESS(before==expected,nexus::Invalid);
    Path from(Name(c)),to(Name(c)+".complete");R_TRY(fsFsRenameFile(&d.sd,from.text,to.text));R_TRY(fsFsCommit(&d.sd));
    nexus::Log("LR1-move-checkpoint-complete",0,c.application);return 0;
}
}
