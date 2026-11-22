// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/repair_catalog.hpp"
#include "nexus/repair_catalog_key.hpp"
#include "nexus/support_storage.hpp"
#include "nexus/update.hpp"
#include "nexus/localize.hpp"
#include "yati/nx/nxdumptool_rsa.h"
#include "ui/progress_box.hpp"
#include <mutex>
namespace nexus::catalog {
namespace {
std::mutex mutex;
constexpr const char* Slots[]{"/switch/nexusmanager/repair-catalog/catalog-0.nxrc","/switch/nexusmanager/repair-catalog/catalog-1.nxrc"};
bool DecodeBytes(const Bytes& bytes,Catalog& out){
    return Decode(bytes,updates::VersionCode(updates::Version),out,[](auto data,auto signature){constexpr u8 e[]{1,0,1};return rsa2048VerifySha256BasedPssSignature(data.data(),data.size(),signature.data(),PublicModulus.data(),e,sizeof(e));});
}
Result LoadInternal(Loaded& out,int& active){
    out={};active=-1;R_TRY(romfsInit());ON_SCOPE_EXIT(romfsExit());
    FILE* f=fopen("romfs:/nexus/repair-catalog.nxrc","rb");R_UNLESS(f,Invalid);ON_SCOPE_EXIT(fclose(f));
    R_UNLESS(!fseek(f,0,SEEK_END),Invalid);const auto size=ftell(f);R_UNLESS(size>0&&size<=long(MaxBytes)&&!fseek(f,0,SEEK_SET),Invalid);
    out.bytes.resize(size);R_UNLESS(fread(out.bytes.data(),1,size,f)==size&&DecodeBytes(out.bytes,out.catalog),Invalid);
    support::Storage storage;auto rc=storage.Open();if(R_FAILED(rc)){out.cache_rejected=true;return 0;}
    for(int i=0;i<2;++i){Bytes bytes;rc=storage.Read(Slots[i],MaxBytes,bytes);if(rc==support::Missing)continue;Catalog candidate;
        if(R_FAILED(rc)||!DecodeBytes(bytes,candidate)){out.cache_rejected=true;continue;}
        if(candidate.revision==out.catalog.revision&&bytes!=out.bytes){out.cache_rejected=true;continue;}
        if(!CanReplace(out.catalog,out.bytes,candidate,bytes))continue;
        out.catalog=std::move(candidate);out.bytes=std::move(bytes);out.cached=true;active=i;
    }return 0;
}
}
Result Load(Loaded& out){std::scoped_lock lock(mutex);int active{};return LoadInternal(out,active);}
Result Update(sphaira::ui::ProgressBox* p,std::string& detail){
    std::scoped_lock lock(mutex);Loaded current;int active{};R_TRY(LoadInternal(current,active));
    Bytes downloaded;R_TRY(updates::FetchRepairCatalog(p,downloaded,detail));Catalog next;
    detail="Repair guidance could not be verified or needs a newer Manager. The existing catalogue was kept."_nx;
    R_UNLESS(DecodeBytes(downloaded,next)&&CanReplace(current.catalog,current.bytes,next,downloaded),Invalid);
    if(downloaded==current.bytes){detail="Repair guidance is up to date. Offline repairs are available."_nx;return 0;}
    support::Storage storage;R_TRY(storage.Open());R_TRY(support::Folders(storage,"repair-catalog"));
    detail="Could not save the new repair guidance. The previous catalogue was kept."_nx;
    R_TRY(storage.Write(Slots[active==0?1:0],downloaded));Loaded check;int slot{};R_TRY(LoadInternal(check,slot));R_UNLESS(check.bytes==downloaded,Invalid);
    detail="New repair guidance saved and verified. It is also available offline."_nx;
    Log("RC1-catalog-updated",0,next.revision);return 0;
}
}
