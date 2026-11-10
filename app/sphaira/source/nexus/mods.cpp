// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/mods.hpp"
#include "nexus/nx_Client.hpp"
#include "nexus/bootstrap.h"
#include "nexus/usb_mod_index.hpp"
#include "ui/progress_box.hpp"
#include <array>
#include <cstdio>
#include <cstring>
namespace nexus {
Result Client::RenameNew(u64 gen,const std::string& from,const std::string& to){
    if(!VolumePathValid(from)||!VolumePathValid(to))return Invalid;
    R_TRY(CheckUsbMutationAllowed());
    R_TRY(Check(gen));return serviceDispatchIn(&service,13,gen,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In,SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{from.c_str(),from.size()+1},{to.c_str(),to.size()+1}});
}
Result Client::Flush(u64 gen){R_TRY(CheckUsbMutationAllowed());R_TRY(Check(gen));return serviceDispatchIn(&service,16,gen);}
}
namespace nexus::mods {
namespace {
class Console final:public IO {
    Client client;FsFileSystem sd{};sphaira::ui::ProgressBox* progress;bool open{},usb_check{};
    bool RC(Result r,const std::string& what){if(R_SUCCEEDED(r))return true;rc=r;return Fail(what);}
    bool NativePath(const std::string& p,std::array<char,FS_MAX_PATH>& out){
        if(!nexus_mod::Path(p.data(),p.size(),false)||p.size()+1>=out.size())return Fail("Unsafe SD mod-cache path.");
        out={};out[0]='/';memcpy(out.data()+1,p.data(),p.size());return true;
    }
    bool Parents(const std::string& path){
        for(size_t pos=path.find('/');pos!=path.npos;pos=path.find('/',pos+1)){
            const auto sub=path.substr(0,pos);Info s;if(!Stat(false,sub,s))return false;
            if(s.exists){if(!s.directory)return Fail("SD parent is not a directory: "+sub);continue;}
            std::array<char,FS_MAX_PATH> p;if(!NativePath(sub,p)||!RC(fsFsCreateDirectory(&sd,p.data()),"Creating SD backup/cache folder failed."))return false;
        }return true;
    }
public:
    Result rc{};u64 generation{};
    explicit Console(sphaira::ui::ProgressBox* p):progress(p){}
    ~Console(){if(open)fsFsClose(&sd);}
    bool Begin(u64 gen=0,bool need_usb=true){
        // Never register into a USB-backed sdmc view. Bootstrap guarantees a
        // physical-SD launch; otherwise require an explicit inactive VSD query.
        if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())){
            NexusVsdStatus s{};if(!RC(client.VirtualSdStatus(s),"Cannot verify the physical SD view. Open the USB Loader HOME app."))return false;
            if(s.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED))return Fail("Registration is unavailable inside USB-mapped homebrew.");
        }
        if(!RC(fsOpenSdCardFileSystem(&sd),"Could not open the physical SD card."))return false;open=true;
        usb_check=need_usb;if(need_usb){Status s{};if(!RC(client.Query(s),"USB service unavailable.")||!s.Available())return Fail("Connect a USB drive using the working Nexus USB Mods M2 firmware.");
            generation=gen?gen:s.generation;if(generation!=s.generation)return Fail("USB drive changed. Scan again.");}
        return Check();
    }
    bool Check() override {
        if(progress&&!RC(progress->ShouldExitResult(),"Cancelled. If activation started, use USB Mods > Recover."))return false;
        return !usb_check||RC(client.Check(generation),"USB disconnected or changed. Registration stopped.");
    }
    bool Stat(bool usb,const std::string& path,Info& out) override {
        out={};if(!Check())return false;
        if(usb){nexus::Info s{};const auto r=client.Stat(generation,path,s);if(r==MAKERESULT(140,205))return true;if(!RC(r,"USB stat failed: "+path))return false;out={true,s.directory!=0,s.size};return true;}
        std::array<char,FS_MAX_PATH> p;if(!NativePath(path,p))return false;FsDirEntryType type;
        auto r=fsFsGetEntryType(&sd,p.data(),&type);if(r==MAKERESULT(2,1))return true;if(!RC(r,"SD stat failed: "+path))return false;
        out.exists=true;out.directory=type==FsDirEntryType_Dir;if(out.directory)return true;
        FsFile file{};if(!RC(fsFsOpenFile(&sd,p.data(),FsOpenMode_Read,&file),"Could not open SD file: "+path))return false;s64 size=0;r=fsFileGetSize(&file,&size);fsFileClose(&file);
        if(!RC(r,"SD size query failed.")||size<0)return false;out.size=size;return true;
    }
    bool List(const std::string& path,std::vector<mods::Entry>& out) override {
        if(!Check())return false;std::vector<nexus::Entry> es;if(!RC(client.List(generation,path,es),"Could not read USB mod folder: "+path))return false;
        out.clear();for(auto& e:es)out.push_back({e.name,e.size,e.directory!=0});return true;
    }
    bool Read(bool usb,const std::string& path,size_t limit,Bytes& b) override {
        Info i;if(!Stat(usb,path,i))return false;if(!i.exists||i.directory||i.size>limit)return Fail("Missing or oversized cache file: "+path);
        b.resize(i.size);if(progress)progress->NewTransfer(path);
        FsFile file{};if(!usb){std::array<char,FS_MAX_PATH> p;if(!NativePath(path,p)||!RC(fsFsOpenFile(&sd,p.data(),FsOpenMode_Read,&file),"SD read open failed."))return false;}
        bool ok=true;for(size_t off=0;off<b.size();){
            if(!Check()){ok=false;break;}const auto n=std::min<size_t>(b.size()-off,256*1024);u64 got{};
            const auto r=usb?client.Read(generation,path,off,b.data()+off,n,got):fsFileRead(&file,off,b.data()+off,n,FsReadOption_None,&got);
            if(!RC(r,"Read failed: "+path)||got!=n){ok=Fail("Short or failed read: "+path);break;}off+=n;if(progress)progress->UpdateTransfer(off,b.size());
        }
        if(!usb)fsFileClose(&file);return ok;
    }
    bool New(bool usb,const std::string& path,const Bytes& b) override {
        Info s;if(!Stat(usb,path,s))return false;if(s.exists)return Fail("Refusing to replace an existing file: "+path);
        if(usb){
            // The existing USB CreateFile API truncates! Use a random staging
            // name, flush it, then FAT's non-replacing rename to publish marker.
            if(!path.ends_with("/.nexus-mod-id")||b.size()!=16)return Fail("Only a 16-byte identity marker may be written to USB.");
            std::array<uint8_t,16> nonce;Random(nonce.data(),nonce.size());const auto stage="/.nexus-marker-"+Hash(Bytes(nonce.begin(),nonce.end()))+".tmp";
            if(!Stat(true,stage,s)||s.exists)return Fail("USB staging collision.");
            if(!RC(client.CreateFile(generation,stage,16),"USB marker staging failed.")||!RC(client.Write(generation,stage,0,b.data(),16),"USB marker write failed.")||!RC(client.Flush(generation),"USB flush failed."))return false;
            Bytes check;if(!Read(true,stage,16,check)||check!=b)return Fail("USB marker verification failed.");
            return RC(client.RenameNew(generation,stage,path),"USB marker changed concurrently; not replaced.")&&RC(client.Flush(generation),"USB marker flush failed.");
        }
        if(!Parents(path))return false;std::array<char,FS_MAX_PATH> p;if(!NativePath(path,p))return false;
        if(!RC(fsFsCreateFile(&sd,p.data(),b.size(),0),"SD stage creation failed."))return false;
        FsFile f{};if(!RC(fsFsOpenFile(&sd,p.data(),FsOpenMode_Write,&f),"SD stage open failed."))return false;bool ok=true;
        for(size_t off=0;off<b.size();){if(!Check()){ok=false;break;}const auto n=std::min<size_t>(b.size()-off,256*1024);if(!RC(fsFileWrite(&f,off,b.data()+off,n,FsWriteOption_None),"SD stage write failed.")){ok=false;break;}off+=n;if(progress)progress->UpdateTransfer(off,b.size());}
        if(ok)ok=RC(fsFileFlush(&f),"SD flush failed.");fsFileClose(&f);return ok;
    }
    bool Rename(const std::string& from,const std::string& to) override {
        if(!Check()||!Parents(to))return false;std::array<char,FS_MAX_PATH> a,b;
        return NativePath(from,a)&&NativePath(to,b)&&RC(fsFsRenameFile(&sd,a.data(),b.data()),"SD rename failed; backup/pending record retained.");
    }
    bool Delete(const std::string& path) override {std::array<char,FS_MAX_PATH> p;return Check()&&NativePath(path,p)&&RC(fsFsDeleteFile(&sd,p.data()),"Could not roll back an owned SD cache file.");}
    std::string Hash(const Bytes& b) override {std::array<uint8_t,32> hash;sha256CalculateHash(hash.data(),b.data(),b.size());std::string out;for(auto c:hash){char s[3];snprintf(s,3,"%02x",c);out+=s;}return out;}
    void Random(void* p,size_t n) override {randomGet(p,n);}
    Result Finish(bool ok,std::string& detail){detail=error;const Result r=ok?0:rc?rc:Invalid;Log("usb-mods",r,generation);return r;}
};
}
Result Find(sphaira::ui::ProgressBox* box,const std::string& folder,std::vector<Candidate>& result,u64& gen,std::string& detail){Console io(box);const bool ok=io.Begin()&&Scan(io,folder,result);gen=io.generation;return io.Finish(ok,detail);}
Result Review(sphaira::ui::ProgressBox* box,const Candidate& c,bool exefs,u64 gen,Plan& plan,std::string& detail){Console io(box);return io.Finish(io.Begin(gen)&&Prepare(io,c,exefs,plan),detail);}
Result Install(sphaira::ui::ProgressBox* box,Plan& plan,u64 gen,std::string& detail){Console io(box);return io.Finish(io.Begin(gen)&&Apply(io,plan),detail);}
Result Restore(sphaira::ui::ProgressBox* box,std::string& detail){Console io(box);return io.Finish(io.Begin(0,false)&&Recover(io),detail);}
}
