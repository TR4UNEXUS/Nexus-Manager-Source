// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/manager.hpp"
#include "nexus/safety.hpp"
#include "nexus/homebrew.hpp"
#include "nexus/library.hpp"
#include "nexus/port_catalog.hpp"
#include "nexus/home_badges.hpp"
#include "nexus/completion.hpp"
#include "nexus/dump.hpp"
#include "nexus/overall_transfer.hpp"
#include "nexus/installed_move_core.hpp"
#include "nexus/startup_trace.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/yati.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "title_info.hpp"
#include "defines.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <cstdio>

namespace sphaira::nexusapp {
namespace {
constexpr Result Invalid = nexus::Invalid;
struct CopyTimer {
    u64& elapsed;
    u64 start{armGetSystemTick()};
    ~CopyTimer(){elapsed+=armTicksToNs(armGetSystemTick()-start)/1000;}
};
struct Storage {
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    u64 generation{};
    Target target{};
    ~Storage() { ncmContentMetaDatabaseClose(&db); ncmContentStorageClose(&cs); }
    Result Open(Target t, bool database = true) {
        target=t;
        R_TRY(OpenStorage(t,cs,&generation));
        if(database) R_TRY(ncmOpenContentMetaDatabase(&db,nexus::LogicalStorage(t)));
        R_SUCCEED();
    }
};
Result CheckStorage(Storage& s) {
    if(s.target==Target::Usb){nexus::Client c;return c.Check(s.generation);}
    R_SUCCEED();
}
std::string UsbContentPath(const NcmContentId& id) {
    // C21 Host and pinned USB storage both use the Fat16K NCM path scheme:
    // SHA256 of the 16 raw ID bytes, first digest byte as an 8-digit directory.
    // Do not use GetPath here: its Horizon-facing availability check may reject
    // an uncopied/unregistered destination or resolve a different SD duplicate.
    static_assert(sizeof(id)==16);
    u8 hash[32];sha256CalculateHash(hash,&id,sizeof(id));
    char path[64]{};
    snprintf(path,sizeof(path),"/registered/%08X/%s.nca",static_cast<unsigned>(hash[0]),HexId(&id).c_str());
    return path;
}
Result InspectContent(Storage& s,const NcmContentInfo& info,bool& present) {
    present=false;u64 expected{};ncmContentInfoSizeToU64(&info,&expected);
    R_TRY(CheckStorage(s));
    if(s.target==Target::Usb) {
        // Routed Has/GetSize may describe an SD duplicate; inspect USB itself.
        nexus::Client c;nexus::Info physical{};
        R_TRY(c.Check(s.generation));
        const auto path=UsbContentPath(info.content_id);
        const auto rc=c.Stat(s.generation,path,physical);
        if(rc==MAKERESULT(140,205))R_SUCCEED(); // owner's FileNotFound, not any I/O error
        R_TRY(rc);
        R_UNLESS(!physical.directory && physical.size==expected,Invalid);
        present=true;
    } else {
        R_TRY(ncmContentStorageHas(&s.cs,&present,&info.content_id));
        if(present){
            s64 size{};R_TRY(ncmContentStorageGetSizeFromContentId(&s.cs,&size,&info.content_id));
            R_UNLESS(size>=0 && static_cast<u64>(size)==expected,Invalid);
        }
    }
    R_SUCCEED();
}
struct Meta { NcmContentMetaKey key{}; std::vector<u8> raw; std::vector<NcmContentInfo> infos; };
// Stack-owned only. Not a saved receipt or a capability accepted from callers.
// Keep the exact physical source/destination sessions open through removal.
struct TransferSession {
    Storage source,destination;
    std::vector<Meta> verified_metas;
    u64 application{},verified_bytes{};
    bool ready{};
    TransferSession()=default;
    TransferSession(const TransferSession&)=delete;
    TransferSession& operator=(const TransferSession&)=delete;
};
bool SameVerifiedMeta(const Meta& current,const Meta& checked) {
    if(!nexus::SameMetaKey(current.key,checked.key) || current.raw!=checked.raw || current.infos.size()!=checked.infos.size())return false;
    // Content infos are fetched separately from raw metadata. Bind the exact
    // IDs and sizes verified, even if metadata changed between those calls.
    for(size_t index=0;index<current.infos.size();++index){
        const auto& a=current.infos[index];const auto& b=checked.infos[index];
        u64 a_size{},b_size{};ncmContentInfoSizeToU64(&a,&a_size);ncmContentInfoSizeToU64(&b,&b_size);
        if(a_size!=b_size || a.content_type!=b.content_type || std::memcmp(&a.content_id,&b.content_id,sizeof(a.content_id)))return false;
    }
    return true;
}
struct TransferProgress {
    ui::ProgressBox* box;
    nexus::safety::OverallTransfer work;
    std::string label;
    bool finishing{};
    explicit TransferProgress(ui::ProgressBox* p):box(p){}
    void Show(){box->SetOverallTransfer(work.Offset(),work.Total(),label,finishing);}
    bool Begin(u64 bytes){if(!work.Begin(bytes))return false;label="Preparing files";Show();return true;}
    void Pass(const std::string& name){label=name;Show();}
    bool Advance(u64 bytes){if(!work.Advance(bytes))return false;box->AddTransferBytes(bytes);Show();return true;}
    void Finish(unsigned percent,const std::string& name){
        if(work.Finish(percent)){finishing=true;label=name;Show();}
    }
};
Result LoadMeta(NcmContentMetaDatabase& db, const NcmContentMetaKey& key, Meta& m,bool legacy_scan=false) {
    m.key = key;
    u64 size{}, actual{};
    R_TRY(ncmContentMetaDatabaseGetSize(&db,&size,&key));
    R_UNLESS(size >= sizeof(NcmContentMetaHeader) && size <= 32*1024*1024,Invalid);
    m.raw.resize(size);
    R_TRY(ncmContentMetaDatabaseGet(&db,&key,&actual,m.raw.data(),size));
    R_UNLESS(size == actual,Invalid);
    R_TRY(ncm::GetContentInfos(&db,&key,m.infos));
    const auto* header = reinterpret_cast<const NcmContentMetaHeader*>(m.raw.data());
    R_UNLESS(m.infos.size() == header->content_count,Invalid);
    const u64 minimum=sizeof(*header)+header->extended_header_size+static_cast<u64>(header->content_count)*sizeof(NcmContentInfo)+static_cast<u64>(header->content_meta_count)*sizeof(NcmContentMetaInfo);
    // Older homebrew forwarder generators declared a nonexistent meta reference.
    // Read-only library recognition may inspect this exact layout. Move/delete
    // verification remains strict; no stored metadata is rewritten by the scan.
    const bool legacy=legacy_scan && key.type==NcmContentMetaType_Application &&
        header->content_count==3 && header->content_meta_count==1 &&
        header->extended_header_size==sizeof(NcmApplicationMetaExtendedHeader) &&
        minimum==m.raw.size()+sizeof(NcmContentMetaInfo);
    R_UNLESS(minimum<=m.raw.size() || legacy,Invalid);
    R_SUCCEED();
}
struct SharedReferenceReader {
    static_assert(sizeof(NcmContentMetaHeader)==nexus::installed_move::HeaderSize);
    static_assert(offsetof(NcmContentMetaHeader,extended_header_size)==0);
    static_assert(offsetof(NcmContentMetaHeader,content_count)==2);
    static_assert(sizeof(NcmContentInfo)==0x18);
    ui::ProgressBox* progress;
    Storage& storage;
    const NcmContentMetaKey& key;
    Result Check() { R_TRY(progress->ShouldExitResult());return CheckStorage(storage); }
    Result Size(u64& size) { return ncmContentMetaDatabaseGetSize(&storage.db,&size,&key); }
    Result Header(std::array<u8,nexus::installed_move::HeaderSize>& header,u64& actual) {
        return ncmContentMetaDatabaseGet(&storage.db,&key,&actual,header.data(),header.size());
    }
    Result Infos(s32 offset,NcmContentInfo* infos,size_t capacity,s32& count) {
        return ncmContentMetaDatabaseListContentInfo(&storage.db,&count,infos,capacity,&key,offset);
    }
};
bool ForwarderAddressSpace(Storage& s,const NcmContentInfo& info,const keys::Keys& keys,const nca::Header& header,unsigned& address){
    for(size_t i=0;i<4;++i){
        const auto& fs=header.fs_header[i];const auto& section=header.fs_table[i];
        if(!fs.IsValid()||fs.fs_type!=nca::FileSystemType_PFS0||fs.hash_type!=nca::HashType_HierarchicalSha256||
            section.GetOffsetEnd()<=section.GetOffset()||section.GetOffsetEnd()>header.size)continue;
        const auto& layer=fs.hash_data.hierarchical_sha256_data.pfs0_layer;
        if(!nexus::library::Range(layer.offset,layer.size,section.GetSize())||layer.size<16)continue;
        auto read=[&](u64 offset,size_t size,std::vector<u8>& out)->bool{
            if(size>65536||!nexus::library::Range(offset,size,layer.size))return false;
            const auto absolute=section.GetOffset()+layer.offset+offset,aligned=absolute&~u64{15};const auto pad=absolute-aligned;
            out.resize(size+pad);if(R_FAILED(ReadContent(s.target,s.cs,info.content_id,s.generation,aligned,out.data(),out.size())))return false;
            if(fs.encryption_type==nca::EncryptionType_AesCtr){
                const FsRightsId empty{};if(std::memcmp(&header.rights_id,&empty,sizeof(empty))||header.kaek_index>=3||header.GetKeyGeneration()>0x20)return false;
                auto h=header;if(R_FAILED(nca::DecryptKeak(keys,h)))return false;
                u8 ctr[16]{};crypto::SetCtr(ctr,fs.section_ctr,aligned);Aes128CtrContext ctx;aes128CtrContextCreate(&ctx,h.key_area[2].area,ctr);
                aes128CtrCrypt(&ctx,out.data(),out.data(),out.size());
            }else if(fs.encryption_type!=nca::EncryptionType_None)return false;
            out.erase(out.begin(),out.begin()+pad);return true;
        };
        std::vector<u8> pfs;if(!read(0,16,pfs)||std::memcmp(pfs.data(),"PFS0",4))continue;
        const u64 bytes=16+u64(nexus::library::Read<u32>(pfs,4))*24+nexus::library::Read<u32>(pfs,8);
        if(bytes>65536||!read(0,bytes,pfs))continue;
        u64 offset{},size{};if(!nexus::library::PfsFile(pfs,layer.size,"main.npdm",offset,size)||size<16)continue;
        std::vector<u8> npdm;if(!read(offset,16,npdm)||std::memcmp(npdm.data(),"META",4))continue;
        address=(npdm[12]>>1)&7;return address==1||address==3;
    }
    return false;
}
std::string DetectForwarder(Storage& s,const NcmContentInfo& info,const keys::Keys& keys,Game& game){
    u64 size{};ncmContentInfoSizeToU64(&info,&size);
    if(info.content_type!=NcmContentType_Program || size<sizeof(nca::Header) || size>32*1024*1024)return {};
    nca::Header h{};if(R_FAILED(ReadContent(s.target,s.cs,info.content_id,s.generation,0,&h,sizeof(h))))return {};
    crypto::cryptoAes128Xts(&h,&h,keys.header_key,0,0x200,sizeof(h),false);
    if(!h.IsValid() || h.content_type!=nca::ContentType_Program || h.size!=size)return {};
    for(size_t i=0;i<4;++i){
        const auto& fs=h.fs_header[i];const auto& section=h.fs_table[i];
        const bool ctr=fs.encryption_type==nca::EncryptionType_AesCtr;
        if(!fs.IsValid() || fs.fs_type!=nca::FileSystemType_RomFS || (!ctr && fs.encryption_type!=nca::EncryptionType_None) ||
            fs.hash_type!=nca::HashType_HierarchicalIntegrity || section.GetOffsetEnd()<=section.GetOffset() || section.GetOffsetEnd()>size)continue;
        const auto& tree=fs.hash_data.integrity_meta_info.info_level_hash;
        if(tree.max_layers!=7)continue; // six stored levels, master is the seventh
        const auto& level=tree.levels[5];
        if(level.hash_data_size>64*1024 || !nexus::library::Range(level.logical_offset,level.hash_data_size,section.GetSize()))continue;
        std::vector<u8> data(level.hash_data_size);
        if(R_FAILED(ReadContent(s.target,s.cs,info.content_id,s.generation,section.GetOffset()+level.logical_offset,data.data(),data.size())))continue;
        if(ctr){
            const FsRightsId empty{};
            if(std::memcmp(&h.rights_id,&empty,sizeof(empty)) || h.kaek_index>=3 || h.GetKeyGeneration()>0x20 ||
                ((section.GetOffset()+level.logical_offset)&15))continue;
            // Small homebrew RomFS only; no ticket lookup or rights changes.
            auto copy=h;if(R_FAILED(nca::DecryptKeak(keys,copy)))continue;
            u8 counter[16]{};crypto::SetCtr(counter,fs.section_ctr,section.GetOffset()+level.logical_offset);
            Aes128CtrContext ctx;aes128CtrContextCreate(&ctx,copy.key_area[2].area,counter);
            aes128CtrCrypt(&ctx,data.data(),data.data(),data.size());
        }
        const auto path=nexus::library::ForwarderPath(data);if(!path.empty()){
            game.forwarder_usb_path=nexus::library::UsbForwarderPath(data);
            game.usb_forwarder=!game.forwarder_usb_path.empty();
            std::string checked,usb;bool path_found{},usb_found{};
            game.forwarder_migratable=nexus::library::RootText(data,"nextNroPath",checked,path_found) && path_found && checked==path &&
                nexus::library::RootText(data,"nexusUsbPath",usb,usb_found) && (!usb_found || game.usb_forwarder) &&
                nexus::library::ForwarderArguments(data,path,game.forwarder_args) &&
                ForwarderAddressSpace(s,info,keys,h,game.forwarder_address_space);
            return path;
        }
    }
    return {};
}
Result AppMetas(u64 app, Storage& storage, std::vector<Meta>& metas) {
    std::vector<ncm::ContentStorageRecord> records;
    R_TRY(ns::GetApplicationRecords(app,records));
    for(const auto& r:records) {
        bool has_meta{};
        R_TRY(ncmContentMetaDatabaseHas(&storage.db,&has_meta,&r.key));
        if(!has_meta)continue;
        R_UNLESS(r.key.type >= NcmContentMetaType_Application && r.key.type <= NcmContentMetaType_DataPatch,Invalid);
        Meta m;
        R_TRY(LoadMeta(storage.db,r.key,m));
        // A move includes every base/update/DLC record in this source storage.
        // Partial physical copies are not silently treated as complete games.
        size_t present{};
        for(const auto& info:m.infos) {
            bool has{};R_TRY(InspectContent(storage,info,has));
            if(has)++present;
        }
        if(!present)continue;
        R_UNLESS(present==m.infos.size(),Invalid);
        metas.push_back(std::move(m));
    }
    R_UNLESS(!metas.empty(),Invalid);
    R_SUCCEED();
}
Result HashContent(ui::ProgressBox* p, Target t, Storage& s, const NcmContentInfo& i, std::array<u8,32>& hash, CopyReport* report=nullptr,TransferProgress* progress=nullptr) {
    u64 size{}; ncmContentInfoSizeToU64(&i,&size);
    Sha256Context ctx; sha256ContextCreate(&ctx);
    std::vector<u8> buf(4*1024*1024);
    // One session/path per full-file pass, not a service open/close every 4 MiB.
    // C21 ReadFileAtGeneration checks the same generation under its volume lock
    // on EVERY read request, including Client::Read's bounded subrequests.
    nexus::Client usb;std::string path;
    if(t==Target::Usb){R_TRY(usb.Check(s.generation));path=UsbContentPath(i.content_id);}
    for(u64 off=0;off<size;) {
        if(report){report->offset=off;report->size=size;}
        R_TRY(p->ShouldExitResult());
        const auto count = std::min<u64>(buf.size(),size-off);
        if(t==Target::Usb){
            u64 actual{};R_TRY(usb.Read(s.generation,path,off,buf.data(),count,actual));
            R_UNLESS(actual==count,Invalid);
        }else{R_TRY(ReadContent(t,s.cs,i.content_id,s.generation,off,buf.data(),count));}
        sha256ContextUpdate(&ctx,buf.data(),count);
        off += count;
        if(progress){R_UNLESS(progress->Advance(count),Invalid);}else p->UpdateTransfer(off,size);
    }
    sha256ContextGetHash(&ctx,hash.data());
    R_SUCCEED();
}
Result VerifyCopied(ui::ProgressBox* p,Storage& dst,const NcmContentInfo& i,const std::array<u8,32>& copied_hash,CopyReport& report,TransferProgress* progress=nullptr) {
    // The digest is from the exact bytes successfully read/written in this copy,
    // never a receipt, content-ID prefix or a hash trusted from an earlier run.
    std::array<u8,32> actual{};
    const auto label=std::string("Checking the copy on ")+nexus::TargetName(dst.target);
    if(progress)progress->Pass(label);else p->NewTransfer(label);
    report.At("Reading the destination for verification","move-hash-destination");
    R_TRY(HashContent(p,dst.target,dst,i,actual,&report,progress));
    report.At("Comparing the copied bytes and destination","move-compare-hashes");
    R_UNLESS(copied_hash==actual,Invalid);
    R_SUCCEED();
}
Result VerifyPair(ui::ProgressBox* p, Target source, Storage& src, Storage& dst, const NcmContentInfo& i, CopyReport* report=nullptr,TransferProgress* progress=nullptr) {
    std::array<u8,32> a{},b{};
    const auto original=std::string("Checking existing files on ")+nexus::TargetName(source);
    if(progress)progress->Pass(original);else p->NewTransfer(original);
    if(report)report->At("Reading the original for verification","move-hash-source");
    R_TRY(HashContent(p,source,src,i,a,report,progress));
    const auto copy=std::string("Checking existing files on ")+nexus::TargetName(dst.target);
    if(progress)progress->Pass(copy);else p->NewTransfer(copy);
    if(report)report->At("Reading the destination for verification","move-hash-destination");
    R_TRY(HashContent(p,dst.target,dst,i,b,report,progress));
    if(report)report->At("Comparing the original and copied file","move-compare-hashes");
    R_UNLESS(a == b,Invalid);
    R_SUCCEED();
}
class UsbSource final : public yati::source::Base {
    nexus::Client client;
    u64 generation{}, size{};
    std::string path;
public:
    UsbSource(u64 gen,const std::string& p):generation(gen),path(p) {
        m_open_result = client.Check(gen);
        nexus::Info info{};
        if(R_SUCCEEDED(m_open_result)) m_open_result = client.Stat(gen,p,info);
        if(R_SUCCEEDED(m_open_result) && info.directory) m_open_result = Invalid;
        size=info.size;
    }
    u64 Size() const { return size; }
    Result Read(void* buf,s64 off,s64 length,u64* actual) override {
        *actual=0;
        R_UNLESS(off>=0 && length>=0 && static_cast<u64>(off)<=size,Invalid);
        return client.Read(generation,path,off,buf,std::min<u64>(length,size-off),*actual);
    }
};
}

std::string HexId(const void* ptr,size_t size) {
    static constexpr char hex[]="0123456789abcdef";
    auto bytes=static_cast<const u8*>(ptr);
    std::string out; out.reserve(size*2);
    for(size_t i=0;i<size;++i) { out+=hex[bytes[i]>>4]; out+=hex[bytes[i]&15]; }
    return out;
}
Result OpenStorage(Target target,NcmContentStorage& cs,u64* generation) {
    cs={};
    R_UNLESS(nexus::ValidTarget(target),Invalid);
    if(generation) *generation=0;
    if(target==Target::Internal) return ncmOpenContentStorage(&cs,NcmStorageId_BuiltInUser);
    Result rc{};
    const auto provider=nexus::Detect(rc);
    if(provider==nexus::Provider::Unknown) return rc;
    if(target==Target::Sd) {
        if(provider==nexus::Provider::Absent) return ncmOpenContentStorage(&cs,NcmStorageId_SdCard);
        return serviceDispatch(ncmGetServiceSession(),65030,.out_num_objects=1,.out_objects=&cs.s);
    }
    nexus::Client client; nexus::Status status{};
    R_TRY(client.Query(status));
    R_UNLESS(status.Available(),nexus::Unavailable);
    if(generation) *generation=status.generation;
    return ncmOpenContentStorage(&cs,NcmStorageId_SdCard);
}
Result CollectDumpRecords(u64 app,Target source,u64 generation,std::vector<DumpRecord>& records) {
    records.clear();
    Storage storage;R_TRY(storage.Open(source));
    if(source==Target::Usb)R_UNLESS(storage.generation==generation,nexus::Unavailable);
    std::vector<Meta> metas;R_TRY(AppMetas(app,storage,metas));
    for(auto& m:metas){
        if(std::ranges::any_of(records,[&](const auto& r){return nexus::SameMetaKey(r.key,m.key);}))continue;
        R_UNLESS(records.size()<4096,Invalid);
        records.push_back({m.key,std::move(m.infos)});
    }
    return CheckStorage(storage);
}
Result ReadContent(Target t,NcmContentStorage& cs,const NcmContentId& id,u64 gen,u64 off,void* data,size_t size) {
    if(t!=Target::Usb) return ncmContentStorageReadContentIdFile(&cs,data,size,&id,off);
    nexus::Client c; R_TRY(c.Check(gen));
    const auto path=UsbContentPath(id);
    u64 actual{}; R_TRY(c.Read(gen,path,off,data,size,actual));
    R_UNLESS(actual==size,Invalid);
    R_SUCCEED();
}
Result ListFiles(bool usb,const std::string& path,std::vector<Item>& items,u64& generation) {
    items.clear();
    R_UNLESS(nexus::VolumePathValid(path),Invalid);
    if(usb) {
        nexus::Client c; nexus::Status status{};
        R_TRY(c.Query(status)); R_UNLESS(status.Available(),nexus::Unavailable);
        if(generation) R_UNLESS(generation==status.generation,nexus::Unavailable);
        generation=status.generation;
        std::vector<nexus::Entry> entries;
        R_TRY(c.List(generation,path,entries));
        for(const auto& e:entries) items.push_back({e.name,path=="/"?path+e.name:path+"/"+e.name,e.size,e.directory!=0});
    } else {
        fs::FsNativeSd fs; fs::Dir dir;
        R_TRY(fs.GetFsOpenResult());
        R_TRY(fs.OpenDirectory(path,FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&dir));
        s64 count{}; R_TRY(dir.GetEntryCount(&count));
        R_UNLESS(count>=0 && count<=65536,Invalid);
        std::vector<FsDirectoryEntry> entries;
        R_TRY(dir.ReadAll(entries));
        for(const auto& e:entries) {
            R_UNLESS(memchr(e.name,0,sizeof(e.name)),Invalid);
            const auto child=path=="/"?path+e.name:path+"/"+e.name;
            R_UNLESS(nexus::VolumePathValid(child),Invalid);
            items.push_back({e.name,child,static_cast<u64>(e.file_size),e.type==FsDirEntryType_Dir});
        }
    }
    std::ranges::sort(items,[](const Item& a,const Item& b){return a.directory!=b.directory?a.directory>b.directory:strcasecmp(a.name.c_str(),b.name.c_str())<0;});
    R_SUCCEED();
}
Result Install(ui::ProgressBox* p,bool usb,u64 generation,const std::string& path,Target target,u64 destination_generation,nexus::install::Diagnostic* diagnostic,yati::InstallOutcome* outcome) {
    yati::ConfigOverride cfg{}; cfg.nexus_target=target;
    cfg.diagnostic=diagnostic;cfg.outcome=outcome;if(outcome)*outcome={};
    if(outcome){cfg.ticket_only=false;cfg.skip_base=false;cfg.skip_patch=false;cfg.skip_addon=false;cfg.skip_data_patch=false;cfg.skip_ticket=false;}
    if(diagnostic){diagnostic->Clear();diagnostic->completion.clear();}
    cfg.nexus_destination_generation=destination_generation;
    // Verify original bytes and retain crypto. Installed content needs the
    // System distribution flag; the original package is never modified.
    cfg.convert_to_standard_crypto=false; cfg.lower_master_key=false; cfg.lower_system_version=false;
    cfg.skip_nca_hash_verify=false;cfg.ignore_distribution_bit=false;
    InstallReceipt receipt;receipt.path=path;receipt.usb_source=usb;
    const auto run=[&](yati::source::Base& source,u64 size)->Result {
        if(target==Target::Usb){receipt.source_size=size;R_TRY(SaveInstallReceipt(receipt));cfg.receipt=&receipt;}
        const auto result=yati::InstallFromSource(p,&source,path,cfg,size);
        if(target==Target::Usb){
            receipt.step=R_SUCCEEDED(result)?InstallStep::Complete:InstallStep::Failed;receipt.result=result;
            const auto saved=SaveInstallReceipt(receipt);
            if(R_FAILED(saved)){nexus::Log("P3-install-receipt-save",saved);
                if(diagnostic)diagnostic->completion+="\nRecovery checkpoint could not be saved; keep the source package.";}
        }
        return result;
    };
    if(usb) {
        UsbSource src(generation,path);
        R_TRY(src.GetOpenResult());
        return run(src,src.Size());
    }
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    fs::File file;s64 size{};R_TRY(fs.OpenFile(path,FsOpenMode_Read,&file));R_TRY(file.GetSize(&size));
    R_UNLESS(size>0,Invalid);
    if(target==Target::Usb){receipt.source_size=size;R_TRY(SaveInstallReceipt(receipt));cfg.receipt=&receipt;}
    const auto result=yati::InstallFromFile(p,&fs,path,cfg);
    if(target==Target::Usb){receipt.step=R_SUCCEEDED(result)?InstallStep::Complete:InstallStep::Failed;receipt.result=result;
        const auto saved=SaveInstallReceipt(receipt);if(R_FAILED(saved)){nexus::Log("P3-install-receipt-save",saved);if(diagnostic)diagnostic->completion+="\nRecovery checkpoint could not be saved; keep the source package.";}}
    return result;
}
Result InstallBatch(ui::ProgressBox* p,bool usb,u64 source_generation,nexus::batch::Report& report) {
    namespace batch=nexus::batch;
    // Clear even if destination/source preflight fails before opening a package.
    for(auto& row:report.items)row.detail.clear();
    const auto save_report=[&]{
        // Bounded, local report. Source paths only; never keys, tickets or URLs.
        nexus::Log("batch-progress",report.result,report.Count(batch::State::Installed));
        if(FILE* f=fopen("sdmc:/switch/nexusmanager/batch-last.log","w")){
            fprintf(f,"Nexus Manager batch\nSource: %s\nDestination: %s\nStage: %s\nResult: 0x%08X\n",
                usb?"USB":"SD",nexus::TargetName(report.target),report.stage.c_str(),report.result);
            for(size_t i=0;i<report.items.size();++i){const auto& row=report.items[i];
                fprintf(f,"%zu\t%s\t0x%08X\t%s\n",i+1,batch::StateName(row.state),row.result,row.entry.path.c_str());
                if(!row.detail.empty())fprintf(f,"Details: %s\n",row.detail.c_str());}
            fclose(f);
        }
    };
    const Result rc=[&]() -> Result {
        R_UNLESS(nexus::ValidTarget(report.target) && !report.items.empty() && report.items.size()<=batch::MaxPackages,Invalid);
        batch::Selection validated;
        for(const auto& row:report.items)R_UNLESS(validated.Add(row.entry),Invalid);
        R_UNLESS(validated.Entries().size()==report.items.size(),Invalid);
        report.stage="Check destination";save_report();
        R_TRY(p->ShouldExitResult());
        u64 destination_generation{};
        if(report.target==Target::Usb){
            nexus::Client c;nexus::Status status{};R_TRY(c.Query(status));
            R_UNLESS(status.Available(),nexus::Unavailable);destination_generation=status.generation;
        }
        nexus::Space space{};R_TRY(nexus::QueryDestination(report.target,space));
        // Package sizes (especially NSZ/XCZ) are not installed sizes. The existing
        // installer checks actual expanded content space per package.
        const auto check=[&](const batch::Entry& e)->Result{
            p->SetTitle(e.name);
            if(destination_generation){nexus::Client c;R_TRY(c.Check(destination_generation));}
            if(usb){
                nexus::Client c;nexus::Info info{};R_TRY(c.Check(source_generation));
                R_TRY(c.Stat(source_generation,e.path,info));
                R_UNLESS(!info.directory && info.size==e.size,Invalid);
            }else{
                fs::FsNativeSd fs;R_TRY(fs.GetFsOpenResult());
                FsDirEntryType type{};R_TRY(fs.GetEntryType(e.path,&type));R_UNLESS(type==FsDirEntryType_File,Invalid);
                fs::File file;s64 size{};R_TRY(fs.OpenFile(e.path,FsOpenMode_Read,&file));R_TRY(file.GetSize(&size));
                R_UNLESS(size>=0 && static_cast<u64>(size)==e.size,Invalid);
            }
            return 0;
        };
        p->SetActionName("Check batch / "+std::string(nexus::TargetName(report.target)));
        size_t active{};
        return batch::Run(report,[&]{return p->ShouldExitResult();},check,
            [&](const batch::Entry& e){
                u64 total{},done{};for(size_t j=0;j<report.items.size();++j){const auto size=std::max<u64>(report.items[j].entry.size,1);total+=size;if(j<active)done+=size;}
                ui::ProgressScope package_scope(p,nexus::WorkPart(done,total),nexus::WorkPart(done+std::max<u64>(e.size,1),total));
                nexus::install::Diagnostic detail;
                const auto result=Install(p,usb,source_generation,e.path,report.target,destination_generation,&detail);
                report.items[active].detail=R_FAILED(result)?detail.text:detail.completion;
                return result;
            },
            [&](size_t i,bool starting){
                if(starting){active=i;p->SetActionName("Install "+std::to_string(i+1)+" / "+std::to_string(report.items.size())+" / "+nexus::TargetName(report.target));
                    p->SetItem(report.items[i].entry.name);p->NewTransfer("Opening package");}
                else save_report();
            });
    }();
    report.result=rc;save_report();return rc;
}
std::string Locations(const Game& g) {
    std::string s;
    for(size_t i=0;i<3;++i) if(g.bytes[i]) { if(!s.empty())s+=" + ";s+=nexus::TargetName(static_cast<Target>(i)); }
    return s.empty()?"Unavailable / not verified":s;
}
Result ScanGames(ui::ProgressBox* p,std::vector<Game>& games) {
    // Browsing is an inventory, not a physical-content or integrity check.
    // Fetch current HOME records on every refresh; cached names/icons are
    // loaded by the existing title worker for the visible page only.
    const auto started=armGetSystemTick();
    nexus::startup::Event("L1-library-list","begin");
    std::vector<Game> found;
    const Result rc=[&]() -> Result {
        R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());
        std::array<NsApplicationRecord,128> records{};
        std::set<u64> seen;
        s32 offset=0,count=0;
        do {
            R_TRY(p->ShouldExitResult());
            R_TRY(nsListApplicationRecord(records.data(),records.size(),offset,&count));
            R_UNLESS(count>=0 && count<=static_cast<s32>(records.size()) && offset+count<=16384,Invalid);
            for(s32 r=0;r<count;++r){
                const auto id=records[r].application_id;
                R_UNLESS(id,Invalid);
                if(!seen.insert(id).second)continue;
                Game game;game.id=id;
                char name[32];snprintf(name,sizeof(name),"%016lX",id);game.name=name;
                found.push_back(std::move(game));
            }
            offset+=count;
            p->UpdateTransfer(found.size(),found.size());
        }while(count==static_cast<s32>(records.size()));
        R_TRY(p->ShouldExitResult());
        return 0;
    }();
    nexus::startup::Event("L1-library-list","end",rc,armTicksToNs(armGetSystemTick()-started)/1000000);
    if(R_SUCCEEDED(rc))games=std::move(found); // no partial list after error/cancel
    return rc;
}

// Expensive physical location/badge inspection is explicit, never a prerequisite
// for opening My games. Neither this nor the inventory authorizes a mutation:
// TransferGame, recovery and export still perform their own fresh validation.
static Result ScanGameDetails(ui::ProgressBox* p,std::vector<Game>& games,u64 only_application,bool update_badges) {
    R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());
    games.clear();
    const bool spl_ready=R_SUCCEEDED(splCryptoInitialize());
    ON_SCOPE_EXIT(if(spl_ready)splCryptoExit());
    keys::Keys keys{};const bool can_identify=spl_ready && R_SUCCEEDED(keys::parse_keys(keys,false));
    Storage sd,nand,usb;
    const std::array<Result,3> available{sd.Open(Target::Sd),nand.Open(Target::Internal),usb.Open(Target::Usb)};
    const std::array<Storage*,3> stores{&sd,&nand,&usb};
    std::array<NsApplicationRecord,128> records{};
    s32 offset=0,count=0;
    do {
        R_TRY(p->ShouldExitResult());
        R_TRY(nsListApplicationRecord(records.data(),records.size(),offset,&count));
        R_UNLESS(count>=0 && count<=static_cast<s32>(records.size()) && offset<16384,Invalid);
        for(s32 r=0;r<count;++r) {
            if(only_application && records[r].application_id!=only_application)continue;
            Game g;g.id=records[r].application_id;
            bool badge_known=R_SUCCEEDED(available[2]),usb_program=false;
            char name[32];snprintf(name,sizeof(name),"%016lX",g.id);g.name=name;
            std::vector<ncm::ContentStorageRecord> metas;
            if(R_SUCCEEDED(ns::GetApplicationRecords(g.id,metas))) {
                std::array<std::set<std::string>,3> seen;
                for(const auto& record:metas) for(size_t t=0;t<3;++t) {
                    if(R_FAILED(available[t]))continue;
                    Meta meta; if(R_FAILED(LoadMeta(stores[t]->db,record.key,meta,true))){if(t==2)badge_known=false;continue;}
                    for(const auto& info:meta.infos) {
                        if(!seen[t].insert(HexId(&info.content_id)).second)continue;
                        u64 size{};ncmContentInfoSizeToU64(&info,&size);
                        bool has{};
                        const auto has_rc=ncmContentStorageHas(&stores[t]->cs,&has,&info.content_id);
                        if(R_FAILED(has_rc)){if(t==2)badge_known=false;continue;}
                        if(!has)continue;
                        if(t==2) {
                            nexus::Client c;char common[FS_MAX_PATH]{};std::string path;nexus::Info physical{};
                            if(R_FAILED(c.Check(usb.generation)) || R_FAILED(ncmContentStorageGetPath(&usb.cs,common,sizeof(common),&info.content_id)) ||
                               !nexus::ContentPath(common,false,path)){badge_known=false;continue;}
                            const auto stat_rc=c.Stat(usb.generation,path,physical);
                            if(stat_rc==MAKERESULT(140,205))continue;
                            if(R_FAILED(stat_rc) || physical.directory || physical.size!=size){badge_known=false;continue;}
                            if(info.content_type==NcmContentType_Program &&
                               (record.key.type==NcmContentMetaType_Application || record.key.type==NcmContentMetaType_Patch))usb_program=true;
                        } else {
                            s64 physical_size{};
                            if(R_FAILED(ncmContentStorageGetSizeFromContentId(&stores[t]->cs,&physical_size,&info.content_id)) || physical_size<0 || static_cast<u64>(physical_size)!=size)continue;
                        }
                        g.bytes[t]+=size;
                        // Carafe packages expose their launch contract outside
                        // the small forwarder RomFS. Match both title and the
                        // exact catalogued program content identity before use.
                        if(g.forwarder_path.empty()&&info.content_type==NcmContentType_Program){
                            const auto content=HexId(&info.content_id);
                            for(const auto& known:nexus::port::KnownPorts)if(known.title==g.id&&!known.program.empty()&&known.program==content){
                                g.forwarder_path="sdmc:"+std::string(known.nro);g.forwarder_args=known.args;
                                g.forwarder_address_space=known.address;g.forwarder_migratable=!known.blocked;break;
                            }
                        }
                        if(can_identify && g.forwarder_path.empty() && info.content_type==NcmContentType_Program)
                            g.forwarder_path=DetectForwarder(*stores[t],info,keys,g);
                    }
                }
                g.location_checked=true;
                if(g.usb_forwarder)g.badge_state=2;
                else if(usb_program)g.badge_state=1;
                else if(badge_known && R_SUCCEEDED(CheckStorage(usb)))g.badge_state=0;
            }
            // Cosmetic cache only. Failure must not fail the library scan or
            // change game content. Other installers are picked up by this scan.
            R_TRY(p->ShouldExitResult());
            if(update_badges){
            const auto badge_rc=UpdateHomeBadge(g);
            g.icon_pending=R_FAILED(badge_rc);
            if(g.badge_state>=0 && R_SUCCEEDED(badge_rc)) {
                const auto done=FinishHomeRefresh(g.id);if(R_FAILED(done)){g.icon_pending=true;nexus::Log("P3-badge-completion",done,g.id);}
            } else if(g.badge_state>=0) {
                const auto queued=QueueHomeRefresh(g.id);if(R_FAILED(queued))nexus::Log("P3-badge-queue-save",queued,g.id);
            }
            if(R_FAILED(badge_rc))nexus::Log("home-badge-cache",badge_rc,g.id);
            }
            games.push_back(std::move(g));
            if(only_application)R_SUCCEED();
            p->UpdateTransfer(games.size(),offset+count);
        }
        offset+=count;
    } while(count==static_cast<s32>(records.size()));
    // NROs are browsed directory-at-a-time in Homebrew apps. Never recursively
    // walk port assets just to display the installed-title inventory.
    R_SUCCEED();
}

Result InspectGame(ui::ProgressBox* p,u64 application,Game& game){
    R_UNLESS(application,Invalid);
    const auto started=armGetSystemTick();
    nexus::startup::Event("L1-game-details","begin",0,application);
    std::vector<Game> found;
    auto rc=ScanGameDetails(p,found,application,false);
    if(R_SUCCEEDED(rc))rc=p->ShouldExitResult();
    if(R_SUCCEEDED(rc) && (found.size()!=1 || found[0].id!=application))rc=Invalid;
    if(R_SUCCEEDED(rc))game=std::move(found[0]);
    nexus::startup::Event("L1-game-details","end",rc,armTicksToNs(armGetSystemTick()-started)/1000000);
    return rc;
}

Result RefreshHomeBadges(ui::ProgressBox* p){
    std::vector<Game> games;
    return ScanGameDetails(p,games,0,true);
}

static Result CopyGameInSession(ui::ProgressBox* p,u64 app,Target source,Target target,CopyReport* diagnostic,TransferSession& session,TransferProgress* progress=nullptr) {
    CopyReport local;
    auto& report=diagnostic?*diagnostic:local;report={};
    const auto started=armGetSystemTick();
    nexus::Log("move-begin-M4",0,app);
    nexus::Log("move-route-M4",0,static_cast<u64>(source)|(static_cast<u64>(target)<<8));
    session.ready=false;
    const auto result=[&]() -> Result {
    R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());
    R_UNLESS(nexus::ValidMove(source,target),Invalid);
    if(source==Target::Usb || target==Target::Usb)R_TRY(nexus::CheckUsbMutationAllowed());
    report.At("Opening the source storage","move-open-source");
    auto& src=session.source;auto& dst=session.destination;
    R_TRY(src.Open(source));
    nexus::Destination destination(target);
    report.At("Opening and testing the destination","move-open-destination");
    dst.target=target;R_TRY(destination.Open(dst.cs));dst.generation=destination.Generation();
    report.At("Opening the destination game list","move-open-metadata");
    R_TRY(ncmOpenContentMetaDatabase(&dst.db,nexus::LogicalStorage(target)));
    report.At("Checking the complete original game","move-source-records");
    std::vector<Meta> metas;R_TRY(AppMetas(app,src,metas));
    std::map<std::string,NcmContentInfo> unique;
    // Refuse to overwrite conflicting metadata for the exact same key.
    for(const auto& m:metas) {
        report.At("Comparing existing game information","move-compare-metadata");
        bool existing{};R_TRY(ncmContentMetaDatabaseHas(&dst.db,&existing,&m.key));
        if(existing){Meta stored;R_TRY(LoadMeta(dst.db,m.key,stored));R_UNLESS(stored.raw==m.raw,Invalid);}
        for(const auto& i:m.infos){
            const auto [entry,added]=unique.emplace(HexId(&i.content_id),i);
            R_UNLESS(added || !std::memcmp(&entry->second,&i,sizeof(i)),Invalid);
        }
    }
    u64 required{},total_bytes{};
    for(const auto& [id,info]:unique) {
        report.content=id;report.offset=0;ncmContentInfoSizeToU64(&info,&report.size);
        R_UNLESS(report.size && report.size<=UINT64_MAX-total_bytes,Invalid);total_bytes+=report.size;
        report.At("Checking existing destination files","move-check-existing");
        bool has{};R_TRY(InspectContent(dst,info,has));
        if(!has){u64 size{};ncmContentInfoSizeToU64(&info,&size);R_UNLESS(size<=UINT64_MAX-required,Invalid);required+=size;}
    }
    if(progress)R_UNLESS(progress->Begin(total_bytes),Invalid);
    report.At("Checking free space","move-check-space");
    s64 free{};R_TRY(ncmContentStorageGetFreeSpaceSize(&dst.cs,&free));
    if(target==Target::Usb){
        nexus::Client c;nexus::Space space{};R_TRY(c.GetSpace(dst.generation,space));
        R_UNLESS(required<=space.free,MAKERESULT(2,30));
    }else{R_UNLESS(free>=0 && required<=static_cast<u64>(free),MAKERESULT(2,30));}
    std::vector<u8> buffer(destination.TransferLimit());
    for(const auto& [id,info]:unique) {
        report.content=id;report.offset=0;ncmContentInfoSizeToU64(&info,&report.size);
        report.At("Checking storage before copying","move-check-storage");
        R_TRY(p->ShouldExitResult());R_TRY(CheckStorage(src));R_TRY(destination.Validate());
        u64 size{};ncmContentInfoSizeToU64(&info,&size);
        bool has{};R_TRY(InspectContent(dst,info,has));
        if(has){
            {CopyTimer time{report.reuse_verify_us};R_TRY(VerifyPair(p,source,src,dst,info,&report,progress));}
            report.At("Registering the verified destination file","move-register-existing");
            R_TRY(destination.EnsureRegistered(info.content_id,size));
            ++report.verified;
            continue;
        }
        NcmPlaceHolderId ph{};
        report.At("Creating a temporary destination file","move-create-placeholder");
        R_TRY(ncmContentStorageGeneratePlaceHolderId(&dst.cs,&ph));
        R_TRY(ncmContentStorageCreatePlaceHolder(&dst.cs,&info.content_id,&ph,size));
        bool owned=true;
        ON_SCOPE_EXIT(if(owned){
            const auto gate=destination.Validate();
            const auto cleanup=R_SUCCEEDED(gate)?ncmContentStorageDeletePlaceHolder(&dst.cs,&ph):gate;
            if(R_FAILED(cleanup))nexus::Log("move-placeholder-cleanup",cleanup,app);
        });
        std::string path;
        report.At("Locating the temporary destination file","move-placeholder-path");
        if(target==Target::Usb){
            char common[FS_MAX_PATH]{};
            R_TRY(ncmContentStorageGetPlaceHolderPath(&dst.cs,common,sizeof(common),&ph));
            R_UNLESS(nexus::ContentPath(common,true,path),Invalid);
        }
        const auto label=std::string("Copying to ")+nexus::TargetName(target)+" - file "+std::to_string(report.verified+1)+" of "+std::to_string(unique.size());
        if(progress)progress->Pass(label);else p->NewTransfer(label);
        // One source session per file. The server still checks generation on
        // every bounded Read request; never fall back to a newly mounted drive.
        nexus::Client source_usb;std::string source_path;
        if(source==Target::Usb){R_TRY(source_usb.Check(src.generation));source_path=UsbContentPath(info.content_id);}
        Sha256Context copied;sha256ContextCreate(&copied);
        for(u64 off=0;off<size;){
            report.offset=off;
            report.At("Checking for cancellation","move-cancel");
            R_TRY(p->ShouldExitResult());
            const auto count=std::min<u64>(buffer.size(),size-off);
            report.At("Reading the source file","move-read-source");
            {CopyTimer time{report.read_us};
                if(source==Target::Usb){
                    u64 actual{};R_TRY(source_usb.Read(src.generation,source_path,off,buffer.data(),count,actual));
                    R_UNLESS(actual==count,Invalid);
                }else R_TRY(ReadContent(source,src.cs,info.content_id,src.generation,off,buffer.data(),count));
            }
            report.At("Writing the destination file","move-write-destination");
            {CopyTimer time{report.write_us};R_TRY(destination.Write(dst.cs,ph,path,size,off,buffer.data(),count));}
            {CopyTimer time{report.hash_us};sha256ContextUpdate(&copied,buffer.data(),count);}
            report.copied_bytes+=count;
            off+=count;
            if(progress){R_UNLESS(progress->Advance(count),Invalid);}else p->UpdateTransfer(off,size);
        }
        report.offset=size;
        report.At("Rechecking storage after the copy","move-postcopy-check");
        R_TRY(CheckStorage(src));R_TRY(destination.Validate());
        report.At("Flushing the destination file","move-flush");
        R_TRY(ncmContentStorageFlushPlaceHolder(&dst.cs));
        report.At("Registering the copied file","move-register");
        R_TRY(ncmContentStorageRegister(&dst.cs,&info.content_id,&ph));owned=false;
        report.At("Checking the copied file size","move-check-size");
        R_TRY(InspectContent(dst,info,has));R_UNLESS(has,Invalid);
        std::array<u8,32> copied_hash{};sha256ContextGetHash(&copied,copied_hash.data());
        {CopyTimer time{report.verify_us};R_TRY(VerifyCopied(p,dst,info,copied_hash,report,progress));}
        report.saved_source_bytes+=size;
        report.At("Confirming destination registration","move-ensure-registration");
        R_TRY(destination.EnsureRegistered(info.content_id,size));
        ++report.verified;
        nexus::Log("move-file-verified-M3",0,report.verified);
    }
    // A successful copy changes the launch records, but never deletes the source.
    // Preserve all other keys (base/update/DLC) and reject concurrent changes.
    if(progress)progress->Finish(15,"Saving game information");
    std::vector<ncm::ContentStorageRecord> records;
    report.At("Reading the current HOME game records","move-read-launch-records");
    R_TRY(ns::GetApplicationRecords(app,records));
    for(const auto& m:metas) {
        report.At("Rechecking original game information","move-recheck-metadata");
        Meta current;R_TRY(LoadMeta(src.db,m.key,current));R_UNLESS(current.raw==m.raw,Invalid);
        bool found=false;
        for(auto& r:records)if(nexus::SameMetaKey(r.key,m.key)){r.storage_id=nexus::LogicalStorage(target);found=true;}
        R_UNLESS(found,Invalid);
        R_TRY(CheckStorage(src));R_TRY(destination.Validate());
        report.At("Saving destination game information","move-set-metadata");
        R_TRY(ncmContentMetaDatabaseSet(&dst.db,&m.key,m.raw.data(),m.raw.size()));
    }
    report.At("Committing destination game information","move-commit-metadata");
    R_TRY(ncmContentMetaDatabaseCommit(&dst.db));
    report.At("Rechecking storage before publishing","move-publication-check");
    R_TRY(CheckStorage(src));R_TRY(destination.Validate());
    report.At("Publishing the verified game to HOME","move-publish");
    R_TRY(ns::PushApplicationRecord(app,records.data(),records.size()));
    report.published=true;
    report.At("Refreshing the HOME icon","move-home-refresh");
    R_TRY(ns::InvalidateApplicationControlCache(app));
    nexus::Log("verified-copy-published",0,app);
    // Full-byte verification succeeded for every fresh or reused file. Bind it
    // to this operation and these still-open sessions, not a persistent cache.
    session.verified_metas=std::move(metas);session.application=app;
    session.verified_bytes=total_bytes;session.ready=true;
    if(progress)progress->Finish(40,"Finishing transfer");
    R_SUCCEED();
    }();
    // Preserve the operation's result even when cleanup or log writing fails.
    // No per-chunk SD logging: retain context in memory and save it on return.
    nexus::Log(report.event,result,report.offset);
    const auto elapsed=armTicksToNs(armGetSystemTick()-started)/1000;
    nexus::Log("move-content-size-M3",result,report.size);
    if(!report.content.empty())nexus::Log(("move-content-"+report.content).c_str(),result,app);
    nexus::Log("move-finished-M3",result,report.verified);
    nexus::Log("move-total-us-M3",result,elapsed);
    nexus::Log("move-read-us-M3",result,report.read_us);
    nexus::Log("move-write-us-M3",result,report.write_us);
    nexus::Log("move-hash-us-M3",result,report.hash_us);
    nexus::Log("move-verify-us-M3",result,report.verify_us);
    nexus::Log("move-reuse-verify-us-M3",result,report.reuse_verify_us);
    nexus::Log("move-copied-bytes-M3",result,report.copied_bytes);
    nexus::Log("move-source-reread-saved-bytes-M3",result,report.saved_source_bytes);
    return result;
}
Result CopyGame(ui::ProgressBox* p,u64 app,Target source,Target target,CopyReport* diagnostic) {
    TransferSession session;
    return CopyGameInSession(p,app,source,target,diagnostic,session);
}

static Result RemoveVerifiedSourceInSession(ui::ProgressBox* p,u64 app,Target source,Target target,RemovalReport& report,TransferSession* verified=nullptr,TransferProgress* progress=nullptr) {
    report={};
    const char* event="remove-open";
    u64 stage_value{};
    bool deleting{};
    const auto stage=[&](const char* name,const char* tag,u64 value=0){
        if(std::strcmp(event,tag))nexus::Log(tag,0,value);
        report.stage=name;event=tag;stage_value=value;
        if(progress){if(!deleting)progress->Finish(50,"Finishing move - checking game records");}
        else p->NewTransfer(name);
    };
    const Result result=[&]() -> Result {
    stage("Open source and destination","remove-open",app);
    R_TRY(ns::Initialize());ON_SCOPE_EXIT(ns::Exit());
    R_UNLESS(nexus::ValidMove(source,target),Invalid);
    if(source==Target::Usb || target==Target::Usb)R_TRY(nexus::CheckUsbMutationAllowed());
    TransferSession local;
    auto& session=verified?*verified:local;
    auto& src=session.source;auto& dst=session.destination;
    if(verified){
        R_UNLESS(session.ready && session.application==app && src.target==source && dst.target==target,Invalid);
        session.ready=false; // consume once, including on cancellation/failure
        R_TRY(CheckStorage(src));R_TRY(CheckStorage(dst));
    }else{R_TRY(src.Open(source));R_TRY(dst.Open(target));}
    stage("Find complete source records","remove-source-records");
    std::vector<Meta> metas;R_TRY(AppMetas(app,src,metas));
    if(verified){
        R_UNLESS(metas.size()==session.verified_metas.size(),Invalid);
        for(const auto& m:metas)R_UNLESS(std::ranges::any_of(session.verified_metas,[&](const Meta& checked){
            return SameVerifiedMeta(m,checked);
        }),Invalid);
    }
    std::map<std::string,NcmContentInfo> contents;
    stage("Compare destination metadata","remove-compare-metadata");
    for(const auto& original:metas) {
        Meta copy;R_TRY(LoadMeta(dst.db,original.key,copy));R_UNLESS(original.raw==copy.raw,Invalid);
        for(const auto& i:original.infos)contents.emplace(HexId(&i.content_id),i);
    }
    const auto all_contents=contents.size();
    stage(verified?"Confirm this operation's verified copy":"Verify destination files and hashes",
        verified?"remove-current-verification-M4":"remove-verify-pairs");
    for(const auto& [id,info]:contents){
        R_TRY(p->ShouldExitResult());R_TRY(CheckStorage(src));R_TRY(CheckStorage(dst));
        bool exists{};R_TRY(InspectContent(dst,info,exists));R_UNLESS(exists,Invalid);
        if(verified){R_TRY(InspectContent(src,info,exists));R_UNLESS(exists,Invalid);}
        else R_TRY(VerifyPair(p,source,src,dst,info));
    }
    // Validate every pair first, then ensure every destination registration
    // before any source metadata/file is deleted (including shared content).
    stage("Ensure destination USB registration","remove-ensure-registration-P2");
    if(target==Target::Usb)for(const auto& [id,info]:contents){
        R_TRY(p->ShouldExitResult());
        u64 size{};ncmContentInfoSizeToU64(&info,&size);
        R_UNLESS(size && size<=INT64_MAX,Invalid);
        R_TRY(nexus::EnsureUsbContentRegistered(dst.generation,info.content_id,static_cast<s64>(size)));
    }
    // Retain any NCA referenced by another metadata record.
    stage("Count shared content references","remove-count-references");
    s32 total{},count{};
    R_TRY(ncmContentMetaDatabaseList(&src.db,&total,&count,nullptr,0,NcmContentMetaType_Unknown,0,0,UINT64_MAX,NcmContentInstallType_Unknown));
    R_UNLESS(total>=0 && total<=65536,Invalid);
    std::vector<NcmContentMetaKey> keys(total);
    stage("Read shared content references","remove-list-references",total);
    R_TRY(ncmContentMetaDatabaseList(&src.db,&total,&count,keys.data(),keys.size(),NcmContentMetaType_Unknown,0,0,UINT64_MAX,NcmContentInstallType_Unknown));
    R_UNLESS(count==static_cast<s32>(keys.size()) && count==total,Invalid);
    for(const auto& key:keys){
        R_TRY(p->ShouldExitResult());
        if(std::ranges::any_of(metas,[&](const Meta& m){return nexus::SameMetaKey(m.key,key);}))continue;
        stage("Check another record for shared files","remove-check-reference",key.id);
        // Do not load whole unrelated database values or validate their unused
        // dependency tails. Only a complete, bounded ContentInfo table can
        // establish shared-file references. Errors still block ALL deletion.
        SharedReferenceReader reader{p,src,key};
        R_TRY(nexus::installed_move::VisitReferences<NcmContentInfo>(reader,[&](const NcmContentInfo& info){
            contents.erase(HexId(&info.content_id));
        },Invalid));
    }
    report.shared=all_contents-contents.size();
    stage("Recheck metadata and device","remove-recheck-metadata");
    R_TRY(CheckStorage(src));R_TRY(CheckStorage(dst));
    for(const auto& m:metas){
        Meta a,b;R_TRY(LoadMeta(src.db,m.key,a));R_TRY(LoadMeta(dst.db,m.key,b));
        R_UNLESS(a.raw==m.raw && b.raw==m.raw,Invalid);
    }
    std::vector<ncm::ContentStorageRecord> published;R_TRY(ns::GetApplicationRecords(app,published));
    stage("Confirm destination launch records","remove-check-launch-records");
    for(const auto& m:metas)R_UNLESS(std::ranges::any_of(published,[&](const auto& r){
        return r.storage_id==nexus::LogicalStorage(target) && nexus::SameMetaKey(r.key,m.key);
    }),Invalid);
    // Resolve/validate every deletion candidate before removing any source data.
    // Never use routed NCM Delete for USB: it may select the verified SD copy.
    std::map<std::string,std::string> source_paths;
    stage("Validate physical source deletion paths","remove-preflight-paths");
    if(source==Target::Usb)for(const auto& [id,info]:contents){
        char common[FS_MAX_PATH]{};std::string path;u64 size{};ncmContentInfoSizeToU64(&info,&size);
        R_TRY(ncmContentStorageGetPath(&src.cs,common,sizeof(common),&info.content_id));
        R_UNLESS(nexus::ContentPath(common,false,path) && nexus::RegisteredFilePath(path),Invalid);
        nexus::Client c;nexus::Info physical{};R_TRY(c.Check(src.generation));
        R_TRY(c.Stat(src.generation,path,physical));
        R_UNLESS(!physical.directory && physical.size==size,Invalid);
        source_paths.emplace(id,std::move(path));
    }
    R_TRY(p->ShouldExitResult());
    // Never remove the logical SD DB: USB and real SD can both depend on it,
    // including copies outside this move. InternalUser has its own database.
    if(source==Target::Usb || target==Target::Usb)R_TRY(nexus::CheckUsbMutationAllowed());
    deleting=true;
    if(progress)progress->Finish(60,std::string("Removing original from ")+nexus::TargetName(source));
    else p->SetActionName(std::string("Removing original from ")+nexus::TargetName(source));
    if(source==Target::Internal && !nexus::ShareMetadata(source,target)){
        stage("Remove original internal metadata","remove-internal-metadata");
        report.removal_started=true;
        for(const auto& m:metas)R_TRY(ncmContentMetaDatabaseRemove(&src.db,&m.key));
        R_TRY(ncmContentMetaDatabaseCommit(&src.db));
    }
    for(const auto& [id,info]:contents) {
        stage("Remove verified source file","remove-file",report.removed);
        R_TRY(p->ShouldExitResult());R_TRY(CheckStorage(src));R_TRY(CheckStorage(dst));
        if(source==Target::Usb || target==Target::Usb)R_TRY(nexus::CheckUsbMutationAllowed());
        report.removal_started=true;
        if(source==Target::Usb){
            const auto& path=source_paths.at(id);u64 size{};ncmContentInfoSizeToU64(&info,&size);
            nexus::Client c;R_TRY(c.DeleteRegistered(src.generation,path,size));
        }else{
            R_TRY(ncmContentStorageDelete(&src.cs,&info.content_id));
        }
        ++report.removed;
        if(progress)progress->Finish(60+static_cast<unsigned>(35*report.removed/std::max<size_t>(1,contents.size())),
            std::string("Removing original from ")+nexus::TargetName(source));
    }
    stage("Refresh HOME menu cache","remove-refresh-cache",report.removed);
    R_TRY(ns::InvalidateApplicationControlCache(app));
    nexus::Log("source-content-removed",0,app);
    R_SUCCEED();
    }();
    if(R_FAILED(result)){nexus::Log(event,result,stage_value);nexus::Log("remove-count-M4",result,report.removed);}
    return result;
}
Result RemoveVerifiedSource(ui::ProgressBox* p,u64 app,Target source,Target target,RemovalReport& report) {
    // Independent cleanup never trusts an old copy/previous run: full rehash.
    return RemoveVerifiedSourceInSession(p,app,source,target,report);
}
Result TransferGame(ui::ProgressBox* p,u64 app,Target source,Target target,bool move,TransferReport& report) {
    report={};report.move=move;
    p->SetActionName(std::string(move?"Moving: ":"Copying: ")+nexus::TargetName(source)+" -> "+nexus::TargetName(target));
    TransferSession session;TransferProgress progress(p);
    R_TRY(p->ShouldExitResult());
    R_TRY(CopyGameInSession(p,app,source,target,&report.copy,session,&progress));
    // No UI gap or unprotected second worker between copy and removal. CopyGame
    // must finish registration AND HOME publication before cleanup is allowed.
    if(move){
        report.phase=TransferPhase::Remove;
        R_TRY(p->ShouldExitResult());
        const auto start=armGetSystemTick();
        const auto result=RemoveVerifiedSourceInSession(p,app,source,target,report.removal,&session,&progress);
        nexus::Log("move-finish-remove-total-us-M4",result,armTicksToNs(armGetSystemTick()-start)/1000);
        R_TRY(result);
        report.copy.final_reread_saved_bytes=session.verified_bytes*2;
        nexus::Log("move-final-reread-saved-bytes-M4",0,report.copy.final_reread_saved_bytes);
    }
    report.phase=TransferPhase::Complete;
    progress.Finish(100,move?"Move complete":"Copy complete");
    R_SUCCEED();
}
Result ScanPlaceholders(ui::ProgressBox* p,Target target,std::vector<Placeholder>& result) {
    result.clear();Storage s;R_TRY(s.Open(target,false));
    std::vector<NcmPlaceHolderId> ids(4096);s32 count{};
    R_TRY(ncmContentStorageListPlaceHolder(&s.cs,ids.data(),ids.size(),&count));
    R_UNLESS(count>=0 && count<static_cast<s32>(ids.size()),Invalid);
    for(s32 i=0;i<count;++i) {
        R_TRY(p->ShouldExitResult());
        s64 size{};R_TRY(ncmContentStorageGetSizeFromPlaceHolderId(&s.cs,&size,&ids[i]));
        R_UNLESS(size>=0,Invalid);
        if(target==Target::Usb) {
            nexus::Client c;nexus::Info info{};char common[FS_MAX_PATH]{};std::string path;
            R_TRY(c.Check(s.generation));
            R_TRY(ncmContentStorageGetPlaceHolderPath(&s.cs,common,sizeof(common),&ids[i]));
            R_UNLESS(nexus::ContentPath(common,true,path),Invalid);
            R_TRY(c.Stat(s.generation,path,info));
            R_UNLESS(!info.directory && info.size==static_cast<u64>(size),Invalid);
        }
        result.push_back({target,ids[i],size,s.generation});
    }
    R_SUCCEED();
}
Result RemovePlaceholder(const Placeholder& item) {
    if(item.target==Target::Usb)R_TRY(nexus::CheckUsbMutationAllowed());
    Storage s;R_TRY(s.Open(item.target,false));
    if(item.target==Target::Usb){
        R_UNLESS(s.generation==item.generation,nexus::Unavailable);
        ncmContentStorageClose(&s.cs);s.cs={};R_TRY(nexus::CheckUsbFirmware());
        R_TRY(serviceDispatchIn(ncmGetServiceSession(),65031,item.generation,
            .out_num_objects=1,.out_objects=&s.cs.s));
    }
    bool exists{};s64 size{};
    R_TRY(ncmContentStorageHasPlaceHolder(&s.cs,&exists,&item.id));
    R_UNLESS(exists,Invalid);
    R_TRY(ncmContentStorageGetSizeFromPlaceHolderId(&s.cs,&size,&item.id));
    R_UNLESS(nexus::SamePlaceholderSnapshot(item.generation,s.generation,item.size,size,exists),Invalid);
    if(item.target==Target::Usb) {
        nexus::Client c;nexus::Info info{};char common[FS_MAX_PATH]{};std::string path;
        R_TRY(c.Check(s.generation));
        R_TRY(ncmContentStorageGetPlaceHolderPath(&s.cs,common,sizeof(common),&item.id));
        R_UNLESS(nexus::ContentPath(common,true,path),Invalid);
        R_TRY(c.Stat(s.generation,path,info));
        R_UNLESS(!info.directory && info.size==static_cast<u64>(size),Invalid);
    }
    // Specific placeholder only. Never CleanupAll, registered NCAs, save data,
    // ticket databases, filesystem recursion or BuiltInSystem storage.
    R_TRY(ncmContentStorageDeletePlaceHolder(&s.cs,&item.id));
    nexus::Log("reviewed-placeholder-removed",0,item.size);
    R_SUCCEED();
}
MetadataStage::~MetadataStage() {
    if(owned)ncmContentStorageDeletePlaceHolder(&cs,&id);
    ncmContentStorageClose(&cs);
}
Result MetadataStage::Open(const std::string& common,fs::FsPath& native) {
    std::string path;
    R_UNLESS(nexus::ContentPath(common,true,path)||nexus::ContentPath(common,false,path),Invalid);
    nexus::Client c;nexus::Status status{};nexus::Info info{};
    R_TRY(c.Query(status));R_UNLESS(status.Available(),nexus::Unavailable);
    R_TRY(c.Stat(status.generation,path,info));
    R_UNLESS(!info.directory && info.size>=0xC00 && info.size<=64*1024*1024,Invalid);
    R_TRY(ncmOpenContentStorage(&cs,NcmStorageId_BuiltInUser));
    R_TRY(ncmContentStorageGeneratePlaceHolderId(&cs,&id));
    NcmContentId cid{};memcpy(&cid,&id,sizeof(cid));
    R_TRY(ncmContentStorageCreatePlaceHolder(&cs,&cid,&id,info.size));owned=true;
    std::vector<u8> buffer(1024*1024);
    for(u64 off=0;off<info.size;) {
        const auto count=std::min<u64>(buffer.size(),info.size-off);u64 actual{};
        R_TRY(c.Read(status.generation,path,off,buffer.data(),count,actual));R_UNLESS(actual==count,Invalid);
        R_TRY(ncmContentStorageWritePlaceHolder(&cs,&id,off,buffer.data(),count));off+=count;
    }
    R_TRY(ncmContentStorageFlushPlaceHolder(&cs));
    return ncmContentStorageGetPlaceHolderPath(&cs,native,sizeof(native),&id);
}
}
