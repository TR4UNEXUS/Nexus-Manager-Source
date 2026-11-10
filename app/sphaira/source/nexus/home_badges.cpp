// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NEXUS_BADGE_HELPER
#include "adapter.hpp"
#else
#include "nexus/home_badges.hpp"
#include "nexus/manager.hpp"
#include "nexus/bootstrap.h"
#include "image.hpp"
#endif
#include "nexus/badge_format.hpp"
#include "nexus/badge_pixels.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

namespace sphaira::nexusapp {
namespace {
constexpr Result Invalid=nexus::Invalid;
struct NativePath {char value[FS_MAX_PATH]{};explicit NativePath(const char* p){snprintf(value,sizeof(value),"%s",p);}};
struct Sd {
    FsFileSystem fs{};
    ~Sd(){if(serviceIsActive(&fs.s))fsFsClose(&fs);}
    Result Open(){
#ifndef NEXUS_BADGE_HELPER
        // Do not put the cache in the USB-backed virtual sdmc of another NRO.
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            nexus::Client c;NexusVsdStatus s{};R_TRY(c.VirtualSdStatus(s));
            R_UNLESS(!(s.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),Invalid);
        }
#endif
        R_TRY(fsOpenSdCardFileSystem(&fs));
        const NativePath directory(nexus_badge::Directory);
        auto rc=fsFsCreateDirectory(&fs,directory.value);
        if(R_FAILED(rc)) {FsDirEntryType type{};R_TRY(Entry(nexus_badge::Directory,type));R_UNLESS(type==FsDirEntryType_Dir,Invalid);}
        R_SUCCEED();
    }
    Result Entry(const char* p,FsDirEntryType& type){const NativePath path(p);return fsFsGetEntryType(&fs,path.value,&type);}
    Result ReadOpen(const char* p,FsFile& f){const NativePath path(p);return fsFsOpenFile(&fs,path.value,FsOpenMode_Read,&f);}
    Result Remove(const char* p){
        const NativePath path(p);auto rc=fsFsDeleteFile(&fs,path.value);
        return rc==MAKERESULT(2,1)?0:rc; // PathNotFound only, not every error.
    }
    Result WriteNew(const char* p,const void* bytes,size_t size){
        const NativePath path(p);R_TRY(fsFsCreateFile(&fs,path.value,size,0));FsFile f{};
        R_TRY(fsFsOpenFile(&fs,path.value,FsOpenMode_Write,&f));
        const auto rc=fsFileWrite(&f,0,bytes,size,FsWriteOption_Flush);fsFileClose(&f);return rc;
    }
    Result Publish(const char* p,const std::vector<u8>& bytes){
        u64 pid{};R_TRY(svcGetProcessId(&pid,CUR_PROCESS_HANDLE));
        char temp[160];snprintf(temp,sizeof(temp),"%s.%llx.%llx.tmp",p,
            static_cast<unsigned long long>(pid),static_cast<unsigned long long>(armGetSystemTick()));
        R_TRY(Remove(temp));R_TRY(WriteNew(temp,bytes.data(),bytes.size()));
        // Re-open and verify before publishing. A missing cache is harmless;
        // no partially written file is ever served as a HOME icon.
        FsFile f{};R_TRY(ReadOpen(temp,f));
        std::vector<u8> check(bytes.size());u64 got{};
        auto rc=fsFileRead(&f,0,check.data(),check.size(),FsReadOption_None,&got);fsFileClose(&f);
        R_TRY(rc);R_UNLESS(got==bytes.size() && check==bytes,Invalid);
        const NativePath from(temp),to(p);
        R_TRY(Remove(p));R_TRY(fsFsRenameFile(&fs,from.value,to.value));return fsFsCommit(&fs);
    }
};
void Path(char* out,size_t size,u64 id,unsigned variant){snprintf(out,size,"%s/%016llx-%u.ubg",nexus_badge::Directory,static_cast<unsigned long long>(id),variant);}
Result Original(u64 id,unsigned variant,NsApplicationControlData& data,u32& actual){
    actual=0;
    if(hosversionAtLeast(21,0,0)){
        Service s{};R_TRY(nsGetReadOnlyApplicationControlDataInterface(&s));
        struct {u8 source,flag,pad[6];u64 id;} in{1,static_cast<u8>(variant),{},id};
        struct {u32 flags[2],size;} out{};
        const auto rc=serviceDispatchInOut(&s,19,in,out,
            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&data,sizeof(data)}});
        serviceClose(&s);if(R_SUCCEEDED(rc)){actual=out.size;return 0;}
        // Fail closed for modern requests. Do not use an unrelated language/
        // resolution variant or change any application control cache.
        return rc;
    }
    R_UNLESS(variant==0,Invalid);u64 size{};
    R_TRY(nsGetApplicationControlData(NsApplicationControlSource_Storage,id,&data,sizeof(data),&size));
    R_UNLESS(size<=UINT32_MAX,Invalid);actual=size;R_SUCCEED();
}
// Best-effort native notification, only after a cache change. Some HOME
// versions keep an additional in-memory image until the next normal refresh.
void RefreshHome(u64 id){
    Service service{};if(R_SUCCEEDED(nsGetApplicationManagerInterface(&service))){
        (void)serviceDispatchIn(&service,404,id);serviceClose(&service);
    }
}
Result VerifyControl(u64 id){
    auto data=std::make_unique<NsApplicationControlData>();
    for(unsigned variant=0;variant<(hosversionAtLeast(21,0,0)?2u:1u);++variant){
        u32 actual{};R_TRY(Original(id,variant,*data,actual));
        R_UNLESS(nexus_badge::Fits(actual,sizeof(*data),4),Invalid);
        const auto icon_size=actual-nexus_badge::NacpSize;
        // Accept the same source image formats as the existing badge path;
        // the strict baseline-JPEG check is for our generated cache only.
        const auto decoded=ImageLoadIcon({data->icon,icon_size});
        R_UNLESS(!decoded.data.empty(),Invalid);
    }
    return 0;
}
}
Result EnableHomeBadges(bool enable){
    Sd sd;R_TRY(sd.Open());constexpr char path[]="/atmosphere/usb_badges/disabled";
    if(enable){R_TRY(sd.Remove(path));}
    else {FsDirEntryType type{};if(R_FAILED(sd.Entry(path,type)))R_TRY(sd.WriteNew(path,"B2",2));}
    return fsFsCommit(&sd.fs);
}
Result UpdateHomeBadge(const Game& game){
    // Unknown/disconnected scans retain the last verified badge. NRO rows
    // have no HOME tile and do not get an invented title ID.
    if(!game.homebrew_path.empty() || !nexus_badge::Title(game.id) || game.badge_state<0)return 0;
    // Disabling cosmetic badges must not turn missing/corrupt original
    // control data into an apparent successful HOME refresh.
    if(game.badge_state>0)R_TRY(VerifyControl(game.id));
    Sd sd;R_TRY(sd.Open());
    FsDirEntryType type{};
    if(R_SUCCEEDED(sd.Entry("/atmosphere/usb_badges/disabled",type)))return 0;
    char path[96];
    if(game.badge_state==0){
        bool removed=false;
        for(unsigned v=0;v<2;++v){Path(path,sizeof(path),game.id,v);
            FsDirEntryType entry{};if(R_SUCCEEDED(sd.Entry(path,entry)))removed=true;
            R_TRY(sd.Remove(path));}
        R_TRY(fsFsCommit(&sd.fs));if(removed)RefreshHome(game.id);return 0;
    }
    auto data=std::make_unique<NsApplicationControlData>();
    Result first_error{};bool changed=false;
    for(unsigned variant=0;variant<(hosversionAtLeast(21,0,0)?2u:1u);++variant){
        const auto result=[&]() -> Result {
            u32 actual{};R_TRY(Original(game.id,variant,*data,actual));
            R_UNLESS(nexus_badge::Fits(actual,sizeof(*data),4),Invalid);
            const size_t original_size=actual-nexus_badge::NacpSize;
            const unsigned dimension=variant?174:256;
            nexus_badge::Header h{};memcpy(h.magic,"NXUBDG1",8);h.version=1;h.header_size=sizeof(h);
            h.title=game.id;h.original_size=original_size;h.dimension=dimension;
            h.flags=game.badge_state==2?2:3; // 1=B1 game, 2=USB homebrew, 3=B2 game
            sha256CalculateHash(h.original_hash,data->icon,original_size);
            Path(path,sizeof(path),game.id,variant);
            // Do not decode/re-encode an unchanged, validated cache.
            FsFile old{};
            if(R_SUCCEEDED(sd.ReadOpen(path,old))){
                nexus_badge::Header previous{};u64 got{};s64 file_size{};
                const bool same=R_SUCCEEDED(fsFileGetSize(&old,&file_size)) && file_size>=0 &&
                    R_SUCCEEDED(fsFileRead(&old,0,&previous,sizeof(previous),FsReadOption_None,&got)) && got==sizeof(previous) &&
                    nexus_badge::Valid(previous,game.id,file_size) && previous.dimension==dimension && previous.flags==h.flags &&
                    previous.original_size==h.original_size && !memcmp(previous.original_hash,h.original_hash,32);
                // Verify cached payload too: a corrupt cache must be repairable.
                if(same){std::vector<u8> jpg(previous.jpeg_size);u8 hash[32];
                    const bool read=R_SUCCEEDED(fsFileRead(&old,sizeof(previous),jpg.data(),jpg.size(),FsReadOption_None,&got)) && got==jpg.size();
                    sha256CalculateHash(hash,jpg.data(),jpg.size());fsFileClose(&old);
                    if(read && !memcmp(hash,previous.jpeg_hash,32) && nexus_badge::Jpeg(jpg.data(),jpg.size(),dimension))return 0;
                }else fsFileClose(&old);
            }
            auto image=ImageLoadIcon({data->icon,original_size});
            R_UNLESS(!image.data.empty(),Invalid);
            if(image.w!=static_cast<int>(dimension) || image.h!=static_cast<int>(dimension))image=ImageResize(image.data,image.w,image.h,dimension,dimension);
            R_UNLESS(nexus_badge::Overlay(image.data.data(),image.data.size(),dimension),Invalid);
            auto jpeg=ImageConvertToJpg(image.data,dimension,dimension);
            R_UNLESS(jpeg.data.size()<=nexus_badge::MaxJpeg && nexus_badge::Jpeg(jpeg.data.data(),jpeg.data.size(),dimension),Invalid);
            h.jpeg_size=jpeg.data.size();sha256CalculateHash(h.jpeg_hash,jpeg.data.data(),jpeg.data.size());
            std::vector<u8> cache(sizeof(h)+jpeg.data.size());memcpy(cache.data(),&h,sizeof(h));memcpy(cache.data()+sizeof(h),jpeg.data.data(),jpeg.data.size());
            R_TRY(sd.Publish(path,cache));changed=true;return 0;
        }();
        if(R_FAILED(result) && !first_error)first_error=result;
    }
    if(changed)RefreshHome(game.id);
    return first_error;
}
}
