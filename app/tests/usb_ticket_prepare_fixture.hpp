#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>
#include <openssl/sha.h>
using u8=std::uint8_t;using u32=std::uint32_t;using u64=std::uint64_t;using s32=std::int32_t;using s64=std::int64_t;using Result=u32;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)
#define R_TRY(call) do{const Result result_=(call);if(result_)return result_;}while(0)
#define R_UNLESS(condition,result) do{if(!(condition))return result;}while(0)
template<class F>struct Scope {F f;~Scope(){f();}};
#define JOIN1(a,b) a##b
#define JOIN(a,b) JOIN1(a,b)
#define ON_SCOPE_EXIT(...) Scope JOIN(scope_,__LINE__){[&](){__VA_ARGS__;}}
using Sha256Context=SHA256_CTX;
void sha256ContextCreate(Sha256Context* c){SHA256_Init(c);}
void sha256ContextUpdate(Sha256Context* c,const void* p,size_t n){SHA256_Update(c,p,n);}
void sha256ContextGetHash(Sha256Context* c,void* p){SHA256_Final(static_cast<u8*>(p),c);}
struct FsRightsId{u8 bytes[16]{};};
enum {NcmContentMetaType_Application=0x80,NcmContentMetaType_Patch=0x81,NcmContentMetaType_AddOnContent=0x82,NcmContentMetaType_DataPatch=0x84};
inline Result gate_error{},transport_error{},extract_error{},report_error{};
inline unsigned archive_writes{},export_reads{},save_attempts{},fail_save_at{},cancel_after{},scan_calls{},es_sessions{};
inline bool running=true,short_export{},bad_export_rights{},oversized{},personal_export{};
inline bool transfer_active{};inline int count_fault{},list_fault{};inline Result list_error{};
inline int ticket_return_delta{},certificate_return_delta{};inline unsigned disconnect_after{},gate_after{};
inline u64 tick{},actual_generation=42;
inline std::function<void()> on_rescan;
namespace sphaira::ui {
struct ProgressBox {
 Result ShouldExitResult(){return cancel_after&&archive_writes>=cancel_after?999:0;}
 void SetTitle(const char*){} void NewTransfer(const char*){} void UpdateTransfer(size_t,size_t){}
};
}
namespace sphaira::yati {
struct MetaKey{u64 id{};u32 version{};u8 type{},install_type{};};
struct ExistingUsbContent{std::array<u8,32> expected_hash{};bool has_expected_hash{};};
struct ExistingUsbRecord{MetaKey key{};std::vector<u8> installed_meta;std::vector<ExistingUsbContent> content;u64 application_id{};};
}
namespace sphaira::nexusapp {
inline constexpr Result MissingUsbTicket=MAKERESULT(140,205),InvalidUsbTicket=MAKERESULT(505,901);
struct UsbTicketPair{std::vector<u8> ticket,certificate;};
enum class UsbImportStatus{Ready,AlreadyRegistered,Incomplete,TicketMissing};
struct UsbImportTitle {yati::ExistingUsbRecord record;UsbImportStatus status{UsbImportStatus::Incomplete};std::string name,reason,meta_path;std::vector<FsRightsId> required_tickets,archived_tickets;};
struct UsbImportScan{u64 generation{};std::vector<UsbImportTitle> titles;bool complete{};};
inline UsbImportScan scan_current;
inline std::map<unsigned,UsbTicketPair> originals,archives;
inline std::set<unsigned> personal,invalid_archives;
Result LoadUsbTicketPair(u64 generation,const FsRightsId& r,UsbTicketPair& pair){if(transport_error)return transport_error;if(generation!=actual_generation)return 88;if(invalid_archives.contains(r.bytes[0]))return InvalidUsbTicket;auto it=archives.find(r.bytes[0]);if(it==archives.end())return MissingUsbTicket;pair=it->second;return 0;}
Result SaveUsbTicketPair(u64 generation,const FsRightsId& r,std::span<const u8> t,std::span<const u8> c){++save_attempts;if(generation!=actual_generation)return 88;if(save_attempts==fail_save_at)return 555;if(archives.contains(r.bytes[0]))return 0;archives[r.bytes[0]]={{t.begin(),t.end()},{c.begin(),c.end()}};++archive_writes;if(archive_writes==disconnect_after)++actual_generation;if(archive_writes==gate_after)gate_error=444;return 0;}
Result ScanUsbImports(ui::ProgressBox*,UsbImportScan& out,u64 gen){++scan_calls;if(on_rescan)on_rescan();if(gen!=actual_generation)return 88;out=scan_current;return 0;}
}
namespace sphaira::es {
enum {TitleKeyType_Common=0,TitleKeyType_Personalized=1};
struct TicketHmac160{u8 data[0x1c0];};
struct TicketData{FsRightsId rights_id;u8 title_key_type{};};
bool IsRightsIdValid(const FsRightsId& r){return std::any_of(std::begin(r.bytes),std::end(r.bytes),[](u8 b){return b;});}
Result Initialize(){++es_sessions;return 0;}void Exit(){--es_sessions;}
Result IsRightsIdCommon(const FsRightsId& r,bool* out){*out=nexusapp::originals.contains(r.bytes[0]);return 0;}
Result IsRightsIdPersonalised(const FsRightsId& r,bool* out){*out=nexusapp::personal.contains(r.bytes[0]);return 0;}
Result CountCommonTicket(s32* count){if(list_error)return list_error;*count=count_fault?count_fault:static_cast<s32>(nexusapp::originals.size());return 0;}
Result CountPersonalizedTicket(s32* count){*count=nexusapp::personal.size();return 0;}
Result ListCommonTicket(s32* written,FsRightsId* ids,s32 count){assert(*written==0);assert(count==static_cast<s32>(nexusapp::originals.size()));s32 i{};for(const auto& [id,pair]:nexusapp::originals){(void)pair;ids[i].bytes[0]=id;++i;}*written=list_fault?list_fault:i;return 0;}
Result ListPersonalizedTicket(s32* written,FsRightsId* ids,s32 count){assert(*written==0);assert(count==static_cast<s32>(nexusapp::personal.size()));s32 i{};for(auto id:nexusapp::personal){ids[i].bytes[0]=id;++i;}*written=i;return 0;}
Result GetCommonTicketAndCertificateSize(u64* ts,u64* cs,const FsRightsId* r){if(extract_error)return extract_error;const auto it=nexusapp::originals.find(r->bytes[0]);if(it==nexusapp::originals.end())return 12;*ts=oversized?65537:it->second.ticket.size();*cs=it->second.certificate.size();return 0;}
Result GetCommonTicketAndCertificateData(u64* ts,u64* cs,void* t,u64 tn,void* c,u64 cn,const FsRightsId* r){++export_reads;if(extract_error)return extract_error;const auto& p=nexusapp::originals.at(r->bytes[0]);assert(tn==p.ticket.size()&&cn==p.certificate.size());memcpy(t,p.ticket.data(),tn);memcpy(c,p.certificate.data(),cn);*ts=short_export?tn-1:tn+ticket_return_delta;*cs=cn+certificate_return_delta;if(bad_export_rights)static_cast<u8*>(t)[0]^=1;if(personal_export)static_cast<u8*>(t)[16]=1;return 0;}
Result GetTicketData(std::span<const u8> t,TicketData* out){if(t.size()<sizeof(*out))return 2;memcpy(out,t.data(),sizeof(*out));return 0;}
}
namespace nexus {
inline constexpr Result Invalid=77,Unavailable=78;
Result CheckUsbMutationAllowed(){return gate_error;}
namespace lifecycle{bool CanPoll(){return running;}}
namespace transfer{enum class Mode{Off,Active};Mode Active(){return transfer_active?Mode::Active:Mode::Off;}}
struct Client{Result Check(u64 generation){return transport_error?transport_error:generation==actual_generation?0:88;}};
}
constexpr size_t FS_MAX_PATH=0x301;
enum FsDirEntryType{FsDirEntryType_File,FsDirEntryType_Dir};
enum{FsOpenMode_Read,FsOpenMode_Write,FsWriteOption_Flush,FsReadOption_None};
struct FsFileSystem{};struct FsFile{std::string path;};
inline std::map<std::string,std::vector<char>> report_files;
inline std::set<std::string> report_dirs;
u64 armGetSystemTick(){return ++tick;}
Result fsOpenSdCardFileSystem(FsFileSystem*){return report_error;}
void fsFsClose(FsFileSystem*){}
Result fsFsGetEntryType(FsFileSystem*,const char* p,FsDirEntryType* type){if(report_dirs.contains(p)){*type=FsDirEntryType_Dir;return 0;}if(report_files.contains(p)){*type=FsDirEntryType_File;return 0;}return MAKERESULT(2,1);}
Result fsFsCreateDirectory(FsFileSystem*,const char* p){report_dirs.insert(p);return 0;}
Result fsFsCreateFile(FsFileSystem*,const char* p,size_t n,int){if(report_files.contains(p))return 11;report_files[p].resize(n);return 0;}
Result fsFsOpenFile(FsFileSystem*,const char* p,int,FsFile* f){if(!report_files.contains(p))return 11;f->path=p;return 0;}
Result fsFileWrite(FsFile* f,u64 off,const void* p,size_t n,int){auto& b=report_files[f->path];assert(off+n<=b.size());memcpy(b.data()+off,p,n);return 0;}
void fsFileClose(FsFile*){}
Result fsFileGetSize(FsFile* f,s64* n){*n=report_files[f->path].size();return 0;}
Result fsFileRead(FsFile* f,u64 off,void* p,size_t n,int,u64* got){auto& b=report_files[f->path];assert(off+n<=b.size());memcpy(p,b.data()+off,n);*got=n;return 0;}
Result fsFsCommit(FsFileSystem*){return 0;}
Result fsFsRenameFile(FsFileSystem*,const char* from,const char* to){if(report_files.contains(to))return 11;report_files[to]=std::move(report_files.at(from));report_files.erase(from);return 0;}
