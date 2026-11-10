// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/completion.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/bootstrap.h"
#include "defines.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sphaira::nexusapp {
namespace {
constexpr Result Missing=MAKERESULT(2,1);
constexpr char Root[]="/switch/usbloader/completion-p3";
struct Record {
    u32 magic=0x3350584E, version=1;
    u64 sequence{}, title{}, source_size{};
    u32 kind{}, state{}, verified{}, result{}, usb_source{}, reserved{};
    char path[768]{};
    u8 checksum[32]{};
};
static_assert(sizeof(Record)==856);
struct Path {char value[FS_MAX_PATH]{};explicit Path(const std::string& p){snprintf(value,sizeof(value),"%s",p.c_str());}};
bool Valid(const Record& r) {
    u8 digest[32];sha256CalculateHash(digest,&r,offsetof(Record,checksum));
    return r.magic==0x3350584E && r.version==1 && r.sequence && r.sequence<UINT64_MAX &&
        r.reserved==0 && !memcmp(digest,r.checksum,32) && memchr(r.path,0,sizeof(r.path)) &&
        ((r.kind==1 && r.usb_source<=1 && r.source_size && r.state>=1 && r.state<=6 && nexus::VolumePathValid(r.path)) ||
         (r.kind==2 && r.title && r.state<=1 && !r.usb_source && !r.source_size && !r.path[0]));
}
struct Disk {
    FsFileSystem fs{};
    ~Disk(){if(serviceIsActive(&fs.s))fsFsClose(&fs);}
    Result Open(){
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            nexus::Client c;NexusVsdStatus v{};R_TRY(c.VirtualSdStatus(v));
            R_UNLESS(!(v.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)),nexus::Invalid);
        }
        R_TRY(fsOpenSdCardFileSystem(&fs));
        for(const char* d:{"/switch","/switch/usbloader",Root}){
            Path p(d);FsDirEntryType type{};auto rc=fsFsGetEntryType(&fs,p.value,&type);
            if(rc==Missing){R_TRY(fsFsCreateDirectory(&fs,p.value));}
            else {R_TRY(rc);R_UNLESS(type==FsDirEntryType_Dir,nexus::Invalid);}
        }
        return 0;
    }
    Result Slot(const std::string& name,Record& r,bool& valid,bool& present){
        valid=present=false;FsFile f{};Path p(name);auto rc=fsFsOpenFile(&fs,p.value,FsOpenMode_Read,&f);
        if(rc==Missing)return 0;R_TRY(rc);present=true;s64 size{};u64 got{};
        rc=fsFileGetSize(&f,&size);
        if(R_SUCCEEDED(rc) && size==sizeof(r))rc=fsFileRead(&f,0,&r,sizeof(r),FsReadOption_None,&got);
        fsFileClose(&f);R_TRY(rc);valid=size==sizeof(r) && got==sizeof(r) && Valid(r);return 0;
    }
    Result Load(const std::string& base,Record& out,bool& found){
        Record a{},b{};bool va{},vb{},pa{},pb{};
        R_TRY(Slot(base+".a",a,va,pa));R_TRY(Slot(base+".b",b,vb,pb));
        // One torn slot is recoverable; two unreadable slots are never "empty".
        R_UNLESS(va || vb || (!pa && !pb),nexus::Invalid);
        if(va && vb && a.sequence==b.sequence)R_UNLESS(!memcmp(&a,&b,sizeof(a)),nexus::Invalid);
        found=va||vb;if(found)out=va && (!vb || a.sequence>=b.sequence)?a:b;return 0;
    }
    Result Save(const std::string& base,Record r){
        Record prior{};bool exists{};R_TRY(Load(base,prior,exists));
        r.sequence=exists?prior.sequence+1:1;R_UNLESS(r.sequence<UINT64_MAX,nexus::Invalid);
        sha256CalculateHash(r.checksum,&r,offsetof(Record,checksum));R_UNLESS(Valid(r),nexus::Invalid);
        const auto name=base+(r.sequence%2?".a":".b");Path path(name);
        FsDirEntryType type{};auto rc=fsFsGetEntryType(&fs,path.value,&type);
        if(rc==Missing){R_TRY(fsFsCreateFile(&fs,path.value,sizeof(r),0));}
        else {R_TRY(rc);R_UNLESS(type==FsDirEntryType_File,nexus::Invalid);}
        FsFile f{};R_TRY(fsFsOpenFile(&fs,path.value,FsOpenMode_Write,&f));
        rc=fsFileSetSize(&f,sizeof(r));if(R_SUCCEEDED(rc))rc=fsFileWrite(&f,0,&r,sizeof(r),FsWriteOption_Flush);
        fsFileClose(&f);R_TRY(rc);R_TRY(fsFsCommit(&fs));
        Record check{};bool valid{},present{};R_TRY(Slot(name,check,valid,present));
        R_UNLESS(valid && !memcmp(&r,&check,sizeof(r)),nexus::Invalid);return 0;
    }
    Result List(const char prefix,std::vector<Record>& result){
        FsDir d{};Path root(Root);R_TRY(fsFsOpenDirectory(&fs,root.value,FsDirOpenMode_ReadFiles,&d));
        s64 count{};std::vector<std::string> names;Result rc{};
        // Fixed-size directory pages and a checked global bound, never silent truncation.
        do {std::array<FsDirectoryEntry,16> page{};rc=fsDirRead(&d,&count,page.size(),page.data());
            if(R_FAILED(rc))break;
            if(count<0 || count>s64(page.size())){rc=nexus::Invalid;break;}
            for(s64 i=0;i<count;++i){
                if(!memchr(page[i].name,0,sizeof(page[i].name))){rc=nexus::Invalid;break;}
                std::string name=page[i].name;
                const size_t digits=prefix=='i'?64:16;
                if(name.size()==digits+4 && name[0]==prefix && name[1]=='-' &&
                    (name.ends_with(".a") || name.ends_with(".b")) &&
                    std::all_of(name.begin()+2,name.end()-2,[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
                    names.push_back(name.substr(0,name.size()-2));
            }
            if(names.size()>32768){rc=nexus::Invalid;break;}
        }while(count && R_SUCCEEDED(rc));
        fsDirClose(&d);R_TRY(rc);std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
        for(const auto& name:names){Record r{};bool found{};R_TRY(Load(std::string(Root)+"/"+name,r,found));if(found)result.push_back(r);}
        return 0;
    }
};
std::string TitleName(char kind,u64 id){char name[80];snprintf(name,sizeof(name),"%s/%c-%016llx",Root,kind,(unsigned long long)id);return name;}
}
Result SaveInstallReceipt(const InstallReceipt& in){
    R_UNLESS(in.path.size()<sizeof(Record::path) && nexus::VolumePathValid(in.path),nexus::Invalid);
    const auto identity=std::string(in.usb_source?"usb:":"sd:")+in.path;u8 hash[32];sha256CalculateHash(hash,identity.data(),identity.size());
    char name[65]{};for(size_t i=0;i<32;++i)snprintf(name+i*2,3,"%02x",hash[i]);
    Record r{};r.kind=1;r.state=u32(in.step);r.title=in.title;r.source_size=in.source_size;r.usb_source=in.usb_source;r.verified=in.verified;r.result=in.result;
    memcpy(r.path,in.path.c_str(),in.path.size()+1);Disk disk;R_TRY(disk.Open());return disk.Save(std::string(Root)+"/i-"+name,r);
}
Result PendingInstalls(std::vector<InstallReceipt>& out){
    out.clear();Disk disk;R_TRY(disk.Open());std::vector<Record> records;R_TRY(disk.List('i',records));
    for(const auto& r:records){R_UNLESS(r.kind==1,nexus::Invalid);if(r.state!=u32(InstallStep::Complete))out.push_back({r.path,r.usb_source!=0,r.source_size,r.title,InstallStep(r.state),r.verified,r.result});}
    return 0;
}
Result QueueHomeRefresh(u64 id){Disk d;R_TRY(d.Open());Record r{};r.kind=2;r.title=id;r.state=1;return d.Save(TitleName('b',id),r);}
Result FinishHomeRefresh(u64 id){Disk d;R_TRY(d.Open());Record r{};bool found{};const auto name=TitleName('b',id);R_TRY(d.Load(name,r,found));if(!found)return 0;R_UNLESS(r.kind==2 && r.title==id,nexus::Invalid);r.state=0;return d.Save(name,r);}
Result PendingHomeRefresh(std::vector<u64>& out){out.clear();Disk d;R_TRY(d.Open());std::vector<Record> rows;R_TRY(d.List('b',rows));for(const auto& r:rows){R_UNLESS(r.kind==2,nexus::Invalid);if(r.state)out.push_back(r.title);}return 0;}
}
