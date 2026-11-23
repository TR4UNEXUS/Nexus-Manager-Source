// SPDX-License-Identifier: GPL-3.0-or-later
#include <nexus/nx_Client.hpp>
#include "nexus/transfer_policy.hpp"
#include "nexus/port_transfer_core.hpp"
#include "nexus/file_manager_core.hpp"
#include <nexus/startup_trace.hpp>
#include <nexus/vsd_policy.hpp>
#include <nexus/bootstrap.h>
#include "nexus/usb_import_core.hpp"
#include "nexus/usb_import_guard.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace nexus {
namespace {
Result OpenNativeSd(NcmContentStorage &s) {
    return serviceDispatch(ncmGetServiceSession(), 65030, .out_num_objects = 1, .out_objects = &s.s);
}
struct Io { u64 offset, generation; };
}
Result CheckUsbMutationAllowed() {
    // The repair journal lives on physical SD, never on a virtual sdmc view.
    if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())) {
        Client client; NexusVsdStatus status{};
        const auto rc=client.VirtualSdStatus(status);
        if(R_FAILED(rc))return rc;
        if(status.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED))return Unavailable;
    }
    FsFileSystem sd{};
    auto rc=fsOpenSdCardFileSystem(&sd);
    if(R_FAILED(rc)) { Log("P2-repair-gate-open",rc); return rc; }
    char path[FS_MAX_PATH]{};
    snprintf(path,sizeof(path),"%s",registration::PendingJournal);
    FsDirEntryType type{};
    rc=fsFsGetEntryType(&sd,path,&type);
    if(rc==MAKERESULT(2,1) && !usbimport::worker_authorized){
        snprintf(path,sizeof(path),"%s",usbimport::Pending);
        rc=fsFsGetEntryType(&sd,path,&type);
    }
    fsFsClose(&sd);
    // Missing is the only permissive result. A file, directory, or I/O failure
    // blocks writes without reading the journal or scanning/hashing the drive.
    if(rc==MAKERESULT(2,1))return 0;
    if(R_SUCCEEDED(rc))rc=registration::RepairPending;
    Log("P2-repair-pending-run-index-recovery",rc);
    return rc;
}
Result CheckUsbFirmware() {
    registration::Capabilities caps{};
    const auto rc=serviceDispatch(ncmGetServiceSession(),registration::CapabilitiesCommand,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&caps,sizeof(caps)}});
    if(R_FAILED(rc)){Log("P3-firmware-preflight-missing",rc);return rc;}
    if(!registration::Compatible(caps)){
        // Numeric capabilities explain mixed/older firmware without collecting
        // configuration paths, credentials or game content.
        Log("firmware-cap-magic",0,caps.magic);Log("firmware-cap-abi",0,caps.abi);
        Log("firmware-cap-titles",0,caps.titles);Log("firmware-cap-content",0,caps.content);
        Log("firmware-cap-features",0,caps.features);Log("firmware-cap-reserved",0,caps.reserved);
        Log("P3-firmware-preflight-incompatible",registration::FirmwareUpdateRequired);
        return registration::FirmwareUpdateRequired;
    }
    return 0;
}
Result EnsureUsbContentRegistered(u64 generation,const NcmContentId& id,s64 expected_size) {
    static_assert(registration::ValidLayout<NcmContentId>());
    const NcmContentId empty{};
    if(!generation || expected_size<=0 || !memcmp(&id,&empty,sizeof(id)))return Invalid;
    auto rc=CheckUsbMutationAllowed();
    if(R_FAILED(rc))return rc;
    Client client;
    rc=client.Check(generation);
    if(R_FAILED(rc)){Log("P2-ensure-generation-before",rc,generation);return rc;}
    const registration::EnsureRequest<NcmContentId> in{id,generation,expected_size};
    rc=serviceDispatchIn(ncmGetServiceSession(),registration::Command,in);
    Log("P2-ensure-usb-content-registration-requires-P2-firmware",rc,generation);
    if(R_FAILED(rc))return rc; // Includes missing command: no simulated success.
    rc=client.Check(generation);
    if(R_FAILED(rc))Log("P2-ensure-generation-after",rc,generation);
    return rc;
}
Provider Detect(Result &rc) {
    startup::Event("usb-service-detect", "begin");
    u8 present = 0;
    const auto name = smEncodeName("ams:usb");
    rc = hosversionAtLeast(12,0,0)
        ? tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, present)
        : serviceDispatchInOut(smGetServiceSession(), 65100, name, present);
    startup::Event("usb-service-detect", "end", rc, present);
    return R_FAILED(rc) ? Provider::Unknown : present ? Provider::Present : Provider::Absent;
}
void Client::Close() { if(serviceIsActive(&service)) serviceClose(&service); service = {}; transfer_size=TransferSize; }
Result Client::Query(Status &status) {
    status = {};
    Result rc = 0;
    if(!serviceIsActive(&service)) {
        auto p = Detect(rc);
        if(p != Provider::Present) return p == Provider::Absent ? Unavailable : rc;
        rc = startup::Call("usb-service-open", [&]{ return smGetService(&service, "ams:usb"); });
        if(R_FAILED(rc)) return rc;
    }
    startup::Event("usb-status-query", "begin");
    rc = serviceDispatchOut(&service, 0, status);
    startup::Event("usb-status-query", "end", rc, status.generation);
    if(R_FAILED(rc)) { status = {}; Close(); }
    else transfer_size = (status.reserved & 1) ? 4*TransferSize : TransferSize;
    return rc;
}
Result Client::Check(u64 generation) {
    Status s{};
    auto rc = Query(s);
    if(R_FAILED(rc)) return rc;
    return generation && s.Available() && generation == s.generation ? 0 : Unavailable;
}
Result Client::VirtualSdStatus(NexusVsdStatus &status) {
    status={};
    Status media{};
    auto rc=Query(media);
    if(R_FAILED(rc))return rc;
    const u64 pid_placeholder=0;
    startup::Event("usb-vsd-query", "begin");
    rc=serviceDispatchInOut(&service,NEXUS_VSD_QUERY,pid_placeholder,status,.in_send_pid=true);
    startup::Event("usb-vsd-query", "end", rc, status.flags);
    if(R_FAILED(rc))return rc;
    return status.magic==NEXUS_VSD_MAGIC && status.version==NEXUS_VSD_VERSION ? 0 : Unavailable;
}
Result Client::ReportSnapshot(std::string& snapshot) {
    snapshot.clear();Status current{};const auto queried=Query(current);if(R_FAILED(queried))return queried;
    std::array<char,8192> data{};
    const auto rc=serviceDispatch(&service,65040,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{data.data(),data.size()}});
    if(R_FAILED(rc))return rc;
    const auto end=std::find(data.begin(),data.end(),char{});if(end==data.end())return Invalid;
    const std::string_view text(data.data(),static_cast<size_t>(end-data.begin()));
    if(!text.starts_with("usb_live_snapshot_version=1\n"))return Invalid;
    snapshot.assign(text);return 0;
}
Result Client::ArmVirtualSd(u64 generation,const std::string &physical_nro) {
    nexus_vsd::Profile profile{};
    if(!nexus_vsd::Parse(physical_nro,profile))return Invalid;
    auto rc=Check(generation);
    if(R_FAILED(rc))return rc;
    NexusVsdStatus status{};
    rc=VirtualSdStatus(status);
    if(R_FAILED(rc))return rc;
    if(status.flags!=NEXUS_VSD_LOADER_SEEN)return Unavailable;
    if(nexus_vsd::UsbRoot(profile) && !(status.reserved&NEXUS_VSD_CAP_USB_ROOT))return Unavailable;
    const struct {u64 pid_placeholder,generation;} in{0,generation};
    return serviceDispatchIn(&service,NEXUS_VSD_ARM,in,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{physical_nro.c_str(),physical_nro.size()+1}},.in_send_pid=true);
}
Result Client::EnsureVirtualSdLoader(NexusVsdStatus &status) {
    auto rc=VirtualSdStatus(status);
    if(R_FAILED(rc) || status.flags==NEXUS_VSD_LOADER_SEEN)return rc;
    // Old/unknown HBLs must not be allowed to arm a view they cannot consume.
    // Register lazily only with the explicit, physical-SD bootstrap contract.
    if(status.flags || !nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize()))return Unavailable;
    const Handle process=envGetOwnProcessHandle();
    if(process==INVALID_HANDLE)return Unavailable;
    const u64 pid_placeholder=0;static const char root[]="/";
    startup::Event("usb-vsd-register", "begin");
    rc=serviceDispatchInOut(&service,NEXUS_VSD_BEGIN_LOAD,pid_placeholder,status,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},.buffers={{root,sizeof(root)}},
        .in_send_pid=true,.in_num_handles=1,.in_handles={process});
    startup::Event("usb-vsd-register", "end",rc,status.flags);
    if(R_FAILED(rc))return rc;
    return status.magic==NEXUS_VSD_MAGIC && status.version==NEXUS_VSD_VERSION &&
        status.flags==NEXUS_VSD_LOADER_SEEN ? 0 : Unavailable;
}
Result Client::CancelVirtualSd() {
    if(!serviceIsActive(&service))return Unavailable;
    const u64 pid_placeholder=0;
    return serviceDispatchIn(&service,NEXUS_VSD_CANCEL,pid_placeholder,.in_send_pid=true);
}
Result Client::Stat(u64 generation, const std::string &path, Info &info) {
    info = {};
    if(!generation || !VolumePathValid(path) || !serviceIsActive(&service)) return Invalid;
    return serviceDispatchInOut(&service, 6, generation, info,
        .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
        .buffers = {{path.c_str(), path.size()+1}});
}
Result Client::Read(u64 generation, const std::string &path, u64 offset, void *buf, size_t size, u64 &actual) {
    actual = 0;
    if(!generation || !VolumePathValid(path) || !serviceIsActive(&service) || size > UINT64_MAX-offset) return Invalid;
    while(actual < size) {
        const auto count = std::min(size - static_cast<size_t>(actual), transfer_size);
        Io in{offset + actual, generation};
        u64 got = 0;
        auto rc = serviceDispatchInOut(&service, 7, in, got,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
            .buffers = {{path.c_str(), path.size()+1}, {static_cast<u8*>(buf)+actual, count}});
        if(R_FAILED(rc)) return rc;
        if(got > count) return Invalid;
        actual += got;
        if(got < count) break; // EOF is visible to the caller, never padded.
    }
    return 0;
}
Result Client::Write(u64 generation, const std::string &path, u64 offset, const void *buf, size_t size) {
    if(!generation || !VolumePathValid(path) || !serviceIsActive(&service) || size > UINT64_MAX-offset) return Invalid;
    const auto gate=CheckUsbMutationAllowed();if(R_FAILED(gate))return gate;
    size_t pos = 0;
    while(pos < size) {
        const auto count = std::min(size-pos, transfer_size);
        Io in{offset+pos, generation};
        u64 wrote = 0;
        auto rc = serviceDispatchInOut(&service, 14, in, wrote,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
            .buffers = {{static_cast<const u8*>(buf)+pos, count}, {path.c_str(), path.size()+1}});
        if(R_FAILED(rc)) return rc;
        if(wrote != count) return Invalid;
        pos += count;
    }
    return 0;
}
Result Client::List(u64 generation, const std::string &path, std::vector<Entry> &entries) {
    entries.clear();
    if(!VolumePathValid(path)) return Invalid;
    auto rc = Check(generation);
    if(R_FAILED(rc)) return rc;
    // Bounded pages and total allocation, with generation checked by the owner.
    std::array<Entry, 32> page{};
    while(entries.size() < 65536) {
        struct { u64 skip, generation; u8 dirs, files, padding[6]; } in{entries.size(), generation, 1, 1, {}};
        u64 count = 0;
        rc = serviceDispatchInOut(&service, 8, in, count,
            .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
            .buffers = {{path.c_str(), path.size()+1}, {page.data(), sizeof(page)}});
        if(R_FAILED(rc) || count > page.size()) { entries.clear(); return R_FAILED(rc) ? rc : Invalid; }
        for(size_t i = 0; i < count; ++i) {
            if(!memchr(page[i].name, 0, sizeof(page[i].name))) { entries.clear(); return Invalid; }
            const std::string name = page[i].name;
            if(name.empty() || name.find('/') != name.npos || !VolumePathValid("/" + name)) { entries.clear(); return Invalid; }
            entries.push_back(page[i]);
        }
        if(count < page.size()) return 0;
    }
    entries.clear();
    return Invalid;
}
Result Client::GetSpace(u64 generation, Space &space) {
    space = {};
    auto rc = Check(generation);
    if(R_SUCCEEDED(rc)) {
        startup::Event("usb-space-query", "begin");
        rc = serviceDispatchOut(&service, 9, space);
        startup::Event("usb-space-query", "end", rc);
    }
    if(R_SUCCEEDED(rc)) rc = Check(generation);
    if(R_FAILED(rc)) space = {};
    return rc;
}
Result Client::CreateFile(u64 generation,const std::string& path,u64 size) {
    if(!VolumePathValid(path))return Invalid;
    const auto gate=CheckUsbMutationAllowed();if(R_FAILED(gate))return gate;
    auto rc=Check(generation);if(R_FAILED(rc))return rc;
    const struct {u64 size,generation;} in{size,generation};
    return serviceDispatchIn(&service,10,in,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Client::CreateDirectory(u64 generation,const std::string& path) {
    if(!VolumePathValid(path))return Invalid;
    const auto gate=CheckUsbMutationAllowed();if(R_FAILED(gate))return gate;
    auto rc=Check(generation);if(R_FAILED(rc))return rc;
    return serviceDispatchIn(&service,15,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Client::DeleteDumpTemporary(u64 generation,const std::string& path) {
    constexpr std::string_view root="/dumps/NSP/";
    if(!VolumePathValid(path) || !path.starts_with(root))return Invalid;
    auto leaf=std::string_view(path).substr(root.size());
    if(leaf.ends_with("/data.nsp"))leaf.remove_suffix(9);
    if(leaf.size()<=12 || !leaf.ends_with(".nsp.partial") || leaf.find('/')!=leaf.npos)return Invalid;
    auto rc=CheckUsbMutationAllowed();if(R_FAILED(rc))return rc;
    rc=Check(generation);if(R_FAILED(rc))return rc;
    return serviceDispatchIn(&service,12,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result QueryPhysicalSd(Space &space) {
    space={};
    // A launcher itself opened through a Virtual SD session cannot label that
    // USB-backed filesystem "microSD". Fail closed rather than misreport it.
    // Our fresh HOME bootstrap explicitly guarantees a real-SD view. Do not
    // make its SD capacity widget wait for the USB owner after another app.
    if(!nexusHasSdBootstrap(envGetLoaderInfo(),envGetLoaderInfoSize())) {
        Client c;NexusVsdStatus status{};
        const auto query=c.VirtualSdStatus(status);
        if(R_SUCCEEDED(query) && (status.flags&(NEXUS_VSD_ACTIVE|NEXUS_VSD_ARMED)))return Unavailable;
    }
    FsFileSystem sd{};
    auto rc=startup::Call("physical-sd-open", [&]{ return fsOpenSdCardFileSystem(&sd); });
    if(R_FAILED(rc))return rc;
    s64 free{},total{};
    static const char root[FS_MAX_PATH]="/";
    rc=startup::Call("physical-sd-free-space", [&]{ return fsFsGetFreeSpace(&sd,root,&free); });
    if(R_SUCCEEDED(rc))rc=startup::Call("physical-sd-total-space", [&]{ return fsFsGetTotalSpace(&sd,root,&total); });
    fsFsClose(&sd);
    if(R_FAILED(rc))return rc;
    if(free<0 || total<=0 || free>total)return Invalid;
    space={static_cast<u64>(free),static_cast<u64>(total)};
    return 0;
}
Result Destination::Open(NcmContentStorage &storage) {
    End();
    storage = {};
    if(!ValidTarget(target)) return Invalid;
    Result rc = 0;
    if(target == Target::Sd) {
        const auto p = Detect(rc);
        if(p == Provider::Unknown) return rc;
        rc = p == Provider::Present ? OpenNativeSd(storage) : ncmOpenContentStorage(&storage, NcmStorageId_SdCard);
        sd_route = SelectSdRoute(p, R_SUCCEEDED(rc));
    } else if(target == Target::Internal) rc = ncmOpenContentStorage(&storage, NcmStorageId_BuiltInUser);
    else {
        rc=CheckUsbFirmware();
        if(R_FAILED(rc))return rc;
        rc=CheckUsbMutationAllowed();
        if(R_FAILED(rc))return rc;
        Status s{};
        rc = client.Query(s);
        if(R_SUCCEEDED(rc) && !s.Available()) rc = Unavailable;
        if(R_SUCCEEDED(rc) && required_generation && s.generation!=required_generation)rc=Unavailable;
        if(R_SUCCEEDED(rc)) {
            generation = s.generation;
            // T1 binds create/write/register/cleanup to one USB generation.
            // An unsupported/failed command is NOT permission to use SD.
            rc = serviceDispatchIn(ncmGetServiceSession(),65031,generation,
                .out_num_objects=1,.out_objects=&storage.s);
            Log("usb-pinned-open-T1",rc,generation);
            // Legacy firmware uses SD-not-present for a revoked USB route.
            // Keep that raw result in the log, but explain the USB destination.
            if(rc==MAKERESULT(2,2001))rc=MAKERESULT(140,208);
        }
        if(R_SUCCEEDED(rc)) rc = Probe(storage);
        if(R_SUCCEEDED(rc)) Log("usb-transfer-bytes",0,client.TransferLimit());
    }
    Log(TargetName(target), rc, generation);
    if(R_FAILED(rc)) {
        if(serviceIsActive(&storage.s)) ncmContentStorageClose(&storage);
        storage = {};
        End();
    } else active = true;
    return rc;
}
Result Client::DeleteInstallTemporary(u64 generation,const std::string& path,u64 expected_size) {
    if(!transfer::TemporaryInstallPath(path)||!expected_size)return Invalid;
    auto rc=CheckUsbMutationAllowed();if(R_FAILED(rc))return rc;
    rc=Check(generation);if(R_FAILED(rc))return rc;
    Info info{};rc=Stat(generation,path,info);if(R_FAILED(rc))return rc;
    if(info.directory||info.size!=expected_size)return Invalid;
    return serviceDispatchIn(&service,12,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Client::DeletePortFile(u64 generation,const std::string& path,u64 expected_size) {
    if(!nexus::port::Safe(path))return Invalid;
    auto rc=CheckUsbMutationAllowed();if(R_FAILED(rc))return rc;
    rc=Check(generation);if(R_FAILED(rc))return rc;
    Info info{};rc=Stat(generation,path,info);if(R_FAILED(rc))return rc;
    if(info.directory || info.size!=expected_size)return Invalid;
    return serviceDispatchIn(&service,12,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Client::DeleteFileManagerEntry(u64 generation,const std::string& path,u64 expected_size,bool directory,const std::string& reviewed_root) {
    files::Snapshot review;review.root=reviewed_root;review.staging=files::StagePath(reviewed_root);
    if(!files::SnapshotPath(review,reviewed_root)||!files::Within(path,reviewed_root)||!files::SnapshotPath(review,path))return Invalid;
    auto rc=CheckUsbMutationAllowed();if(R_FAILED(rc))return rc;
    rc=Check(generation);if(R_FAILED(rc))return rc;
    Info info{};rc=Stat(generation,path,info);if(R_FAILED(rc))return rc;
    if(bool(info.directory)!=directory||(!directory&&info.size!=expected_size)||(directory&&expected_size))return Invalid;
    if(directory){std::vector<Entry> entries;rc=List(generation,path,entries);if(R_FAILED(rc))return rc;if(!entries.empty())return Invalid;}
    return serviceDispatchIn(&service,12,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Client::DeleteRegistered(u64 generation,const std::string& path,u64 expected_size) {
    if(!RegisteredFilePath(path)) return Invalid;
    const auto gate=CheckUsbMutationAllowed();if(R_FAILED(gate))return gate;
    auto rc=Check(generation);
    Info info{};
    if(R_SUCCEEDED(rc))rc=Stat(generation,path,info);
    if(R_FAILED(rc))return rc;
    if(info.directory || info.size!=expected_size)return Invalid;
    // R166 command 12 holds the volume lock and checks generation atomically.
    return serviceDispatchIn(&service,12,generation,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
        .buffers={{path.c_str(),path.size()+1}});
}
Result Destination::Probe(NcmContentStorage &storage) {
    NcmPlaceHolderId ph{};
    auto rc = ncmContentStorageGeneratePlaceHolderId(&storage, &ph);
    if(R_FAILED(rc)) return rc;
    NcmContentId id{};
    memcpy(&id, &ph, sizeof(id));
    rc = ncmContentStorageCreatePlaceHolder(&storage, &id, &ph, 32);
    if(R_FAILED(rc)) return rc; // Never delete an ID we did not create.
    std::array<u8,32> expected{}, actual{};
    randomGet(expected.data(), expected.size());
    char common[FS_MAX_PATH]{};
    std::string path;
    rc = ncmContentStorageWritePlaceHolder(&storage, &ph, 0, expected.data(), expected.size());
    if(R_SUCCEEDED(rc)) rc = ncmContentStorageGetPlaceHolderPath(&storage, common, sizeof(common), &ph);
    if(R_SUCCEEDED(rc) && !ContentPath(common, true, path)) rc = Invalid;
    u64 got = 0;
    if(R_SUCCEEDED(rc)) rc = client.Read(generation, path, 0, actual.data(), actual.size(), got);
    if(R_SUCCEEDED(rc) && (got != actual.size() || actual != expected)) rc = Invalid;
    // Do not send cleanup into a different media generation.
    const auto check = client.Check(generation);
    if(R_SUCCEEDED(check)) {
        const auto clean = ncmContentStorageDeletePlaceHolder(&storage, &ph);
        if(R_SUCCEEDED(rc)) rc = clean;
    } else if(R_SUCCEEDED(rc)) rc = check;
    Log("physical-usb-probe", rc, generation);
    return rc;
}
Result Destination::Validate() {
    if(!active) return Invalid;
    if(target == Target::Usb) {
        const auto gate=CheckUsbMutationAllowed();if(R_FAILED(gate))return gate;
        return client.Check(generation);
    }
    if(target == Target::Sd && sd_route == SdRoute::Stock) {
        Result rc = 0;
        const auto p = Detect(rc);
        return p == Provider::Absent ? 0 : R_FAILED(rc) ? rc : Unavailable;
    }
    return 0;
}
Result Destination::Write(NcmContentStorage &storage, const NcmPlaceHolderId &id, const std::string &path,
    u64 total, u64 offset, const void *buf, size_t size) {
    if(!active || !ValidRange(total,offset,size)) return Invalid;
    if(target != Target::Usb) {
        const auto rc = Validate();
        return R_FAILED(rc) ? rc : ncmContentStorageWritePlaceHolder(&storage, &id, offset, buf, size);
    }
    // The server validates generation on each call. The path belongs to an
    // NCM-created placeholder; it is not created by the raw write endpoint.
    Info info{};
    auto rc = client.Stat(generation, path, info);
    if(R_FAILED(rc)) return rc;
    if(info.directory || info.size != total) return Invalid;
    return client.Write(generation, path, offset, buf, size);
}
Result Destination::Verify(NcmContentStorage &storage, const NcmContentId &id, u64 size) {
    auto rc = Validate();
    if(R_FAILED(rc) || target != Target::Usb) return rc;
    char common[FS_MAX_PATH]{};
    rc = ncmContentStorageGetPath(&storage, common, sizeof(common), &id);
    std::string path;
    if(R_SUCCEEDED(rc) && !ContentPath(common, false, path)) rc = Invalid;
    Info info{};
    if(R_SUCCEEDED(rc)) rc = client.Stat(generation,path,info);
    if(R_SUCCEEDED(rc) && (info.directory || info.size != size)) rc = Invalid;
    Log("registered-usb-content", rc, size);
    if(R_FAILED(rc))return rc;
    return EnsureRegistered(id,size);
}
Result Destination::EnsureRegistered(const NcmContentId &id,u64 size) {
    const auto rc=Validate();
    if(R_FAILED(rc) || target!=Target::Usb)return rc;
    if(!size || size>INT64_MAX)return Invalid;
    return EnsureUsbContentRegistered(generation,id,static_cast<s64>(size));
}
Result QueryDestination(Target target, Space &space) {
    space = {};
    if(target==Target::Sd)return QueryPhysicalSd(space);
    if(target == Target::Usb) {
        Client c; Status s{};
        auto rc = c.Query(s);
        if(R_FAILED(rc)) return rc;
        return s.Available() ? c.GetSpace(s.generation,space) : Unavailable;
    }
    NcmContentStorage storage{};
    Result rc = 0;
    rc = ncmOpenContentStorage(&storage, NcmStorageId_BuiltInUser);
    if(R_FAILED(rc)) return rc;
    s64 free = 0, total = 0;
    rc = ncmContentStorageGetFreeSpaceSize(&storage, &free);
    if(R_SUCCEEDED(rc)) rc = ncmContentStorageGetTotalSpaceSize(&storage, &total);
    ncmContentStorageClose(&storage);
    if(R_SUCCEEDED(rc) && free >= 0 && total >= free) space = {static_cast<u64>(free),static_cast<u64>(total)};
    else if(R_SUCCEEDED(rc)) rc = Invalid;
    return rc;
}
}
