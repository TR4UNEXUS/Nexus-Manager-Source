#include "nexus/localize.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/cartridge.hpp"
#include "nexus/transfer_policy.hpp"
#include "nexus/nx_Client.hpp"
#include "utils/utils.hpp"
#include <array>
#include <memory>
#include <algorithm>
#include <cstring>

namespace sphaira::nexusmanager {
namespace {
struct Card {
    FsDeviceOperator dev{};FsStorage storage{};FsGameCardHandle handle{};
    bool deviceOpen{},storageOpen{};int partition=-1;u64 normal{},secure{},trim{};
    std::array<u8,0x0c> identity{}; // Three 32-bit card IDs, not a 64-byte blob.
    std::array<u8,512> headerIdentity{};
    ~Card(){if(storageOpen)fsStorageClose(&storage);if(deviceOpen)fsDeviceOperatorClose(&dev);}
    Result Identity(){std::array<u8,0x0c> id{};R_TRY(fsDeviceOperatorGetGameCardIdSet(&dev,id.data(),id.size(),id.size()));R_UNLESS(id==identity,nexus::Invalid);return 0;}
    Result VerifyHeader(){R_TRY(Mount(0));alignas(0x1000) u8 header[512]{};R_TRY(fsStorageRead(&storage,0,header,sizeof(header)));R_UNLESS(!std::memcmp(header,headerIdentity.data(),sizeof(header)),nexus::Invalid);return 0;}
    Result Mount(int part){
        if(part==partition)return 0;
        if(storageOpen){fsStorageClose(&storage);storageOpen=false;partition=-1;}
        R_TRY(Identity());Result rc=nexus::Unavailable;
        // A bounded retry permits Horizon's existing secure mount to release.
        for(int i=0;i<3;++i){
            R_TRY(fsDeviceOperatorGetGameCardHandle(&dev,&handle));
            const struct {FsGameCardHandle card;u32 partition;} in={handle,u32(part)};
            rc=serviceDispatchIn(fsGetServiceSession(),30,in,.out_num_objects=1,.out_objects=&storage.s);
            if(R_SUCCEEDED(rc)){storageOpen=true;partition=part;return Identity();}
        }return rc;
    }
    Result Open(){
        R_TRY(fsOpenDeviceOperator(&dev));deviceOpen=true;bool inserted{};
        R_TRY(fsDeviceOperatorIsGameCardInserted(&dev,&inserted));R_UNLESS(inserted,nexus::Unavailable);
        R_TRY(fsDeviceOperatorGetGameCardIdSet(&dev,identity.data(),identity.size(),identity.size()));
        R_TRY(Mount(0));s64 size{};R_TRY(fsStorageGetSize(&storage,&size));R_UNLESS(size>0,nexus::Invalid);normal=size;
        alignas(0x1000) u8 header[512]{};R_TRY(fsStorageRead(&storage,0,header,sizeof(header)));
        std::memcpy(headerIdentity.data(),header,sizeof(header));
        R_UNLESS(!std::memcmp(header+0x100,"HEAD",4),nexus::Invalid);u32 last{};std::memcpy(&last,header+0x118,4);
        R_TRY(Mount(1));R_TRY(fsStorageGetSize(&storage,&size));R_UNLESS(size>0,nexus::Invalid);secure=size;
        R_UNLESS(nexus::transfer::CartridgeGeometry(normal,secure,last,trim),nexus::Invalid);return 0;
    }
    Result Read(u64 off,void* buffer,size_t n){
        R_UNLESS(off%512==0 && n%512==0 && off<=normal+secure && n<=normal+secure-off,nexus::Invalid);
        auto* dst=static_cast<u8*>(buffer);
        while(n){bool first=off<normal;const auto count=std::min<u64>(n,first?normal-off:normal+secure-off);
            R_TRY(Mount(first?0:1));R_TRY(fsStorageRead(&storage,first?off:off-normal,dst,count));
            off+=count;dst+=count;n-=count;
        }return 0;
    }
};
struct Output {
    FsFileSystem fs{};FsFile file{};bool fsOpen{},fileOpen{};
    ~Output(){if(fileOpen)fsFileClose(&file);if(fsOpen)fsFsClose(&fs);}
    Result Directory(const char* path){FsDirEntryType t{};auto rc=fsFsGetEntryType(&fs,path,&t);if(R_SUCCEEDED(rc))return t==FsDirEntryType_Dir?0:nexus::Invalid;return fsFsCreateDirectory(&fs,path);}
};
}
Result Cartridge(ui::ProgressBox* p,bool dump,bool trimmed,std::string& detail){
    detail="Could not read the cartridge. Close any game, keep the cartridge inserted, then try again. No cartridge data is changed."_nx;
    Card card;R_TRY(card.Open());
    if(!dump){detail="Cartridge detected\n\nUsed data: "_nx+utils::formatSizeStorage(card.trim)+"\nFull image: "_nx+utils::formatSizeStorage(card.normal+card.secure)+"\n\nTrimmed export keeps all used data. Both exports remove the private card certificate, then verify the complete written file. Reinsert the cartridge afterwards if HOME does not refresh it."_nx;return 0;}
    const u64 total=trimmed?card.trim:card.normal+card.secure;
    Output out;R_TRY(fsOpenSdCardFileSystem(&out.fs));out.fsOpen=true;
    for(const char* dir:{"/switch","/switch/nexusmanager","/switch/nexusmanager/transfers","/switch/nexusmanager/transfers/exports"})R_TRY(out.Directory(dir));
    s64 free{};R_TRY(fsFsGetFreeSpace(&out.fs,"/",&free));
    detail="Not enough free SD space for this cartridge export. No existing files were changed."_nx;
    R_UNLESS(free>0 && u64(free)>total+32*1024*1024,nexus::Unavailable);
    u64 id{};randomGet(&id,sizeof(id));char filename[160]{};
    std::snprintf(filename,sizeof(filename),"/switch/nexusmanager/transfers/exports/cartridge-%016llx%s.xci",static_cast<unsigned long long>(id),trimmed?"-trimmed":"-full");
    const std::string final=filename,temp=final+".partial";
    detail="Export did not finish. The cartridge is unchanged; any unfinished output remains .partial on SD. Check free space and keep the cartridge inserted."_nx;
    R_TRY(fsFsCreateFile(&out.fs,temp.c_str(),total,total>0xffffffffULL?FsCreateOption_BigFile:0));
    R_TRY(fsFsOpenFile(&out.fs,temp.c_str(),FsOpenMode_Write,&out.file));out.fileOpen=true;
    constexpr size_t Chunk=1024*1024;auto buffer=std::make_unique<u8[]>(Chunk);
    Sha256Context src{},dst{};sha256ContextCreate(&src);sha256ContextCreate(&dst);
    p->SetProgressNote("Copy and complete SHA-256 readback; cartridge stays read-only"_nx);
    for(u64 offset=0;offset<total;){
        R_TRY(p->ShouldExitResult());const auto n=std::min<u64>(Chunk,total-offset);R_TRY(card.Read(offset,buffer.get(),n));
        // Never export the unique game-card certificate in a routine backup.
        const auto lo=std::max<u64>(offset,0x7000),hi=std::min<u64>(offset+n,0x7200);
        if(lo<hi)std::memset(buffer.get()+lo-offset,0xff,hi-lo);
        R_TRY(fsFileWrite(&out.file,offset,buffer.get(),n,FsWriteOption_None));sha256ContextUpdate(&src,buffer.get(),n);
        offset+=n;p->SetOverallTransfer(offset,total*2,"Copying cartridge"_nx);
    }
    R_TRY(card.VerifyHeader());R_TRY(fsFileFlush(&out.file));fsFileClose(&out.file);out.fileOpen=false;
    R_TRY(fsFsOpenFile(&out.fs,temp.c_str(),FsOpenMode_Read,&out.file));out.fileOpen=true;
    for(u64 offset=0;offset<total;){
        R_TRY(p->ShouldExitResult());const auto n=std::min<u64>(Chunk,total-offset);u64 actual{};
        R_TRY(fsFileRead(&out.file,offset,buffer.get(),n,FsReadOption_None,&actual));R_UNLESS(actual==n,nexus::Invalid);
        sha256ContextUpdate(&dst,buffer.get(),n);offset+=n;p->SetOverallTransfer(total+offset,total*2,"Verifying export"_nx);
    }
    std::array<u8,32>a{},b{};sha256ContextGetHash(&src,a.data());sha256ContextGetHash(&dst,b.data());R_UNLESS(a==b,nexus::Invalid);
    fsFileClose(&out.file);out.fileOpen=false;R_TRY(p->ShouldExitResult());
    R_TRY(fsFsRenameFile(&out.fs,temp.c_str(),final.c_str()));R_TRY(fsFsCommit(&out.fs));
    detail="Cartridge export verified\n\nSD:"_nx+final+"\n\nTo install: choose Install games, then SD card and this XCI. To copy it to your PC: open PC transfer. On FAT32, large exports use an archive folder; copy through MTP/FTP as a single file.\n\nPrivate card certificate removed. Original cartridge kept."_nx;
    return 0;
}
Result StorageReport(ui::ProgressBox* p,std::string& detail){
    R_TRY(p->ShouldExitResult());nexus::Space sd{};R_TRY(nexus::QueryPhysicalSd(sd));
    detail="microSD\nFree: "_nx+utils::formatSizeStorage(sd.free)+" / "+utils::formatSizeStorage(sd.total);
    nexus::Client usb;nexus::Status state{};auto rc=usb.Query(state);
    detail+="\n\nUSB companion: "_nx+std::string(R_SUCCEEDED(rc)&&state.Available()?"ready":"not ready / disconnected"_nx);
    detail+="\n\nRead-only status; this is not a corruption check. Use My games > Check / repair for a game, or Files > Calculate SHA-256 for a file. No format or system-storage tools are run."_nx;return 0;
}
}
