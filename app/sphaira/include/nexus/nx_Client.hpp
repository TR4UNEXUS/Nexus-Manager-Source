// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <nexus/nx_Policy.hpp>
#include <nexus/vsd_protocol.h>
#include <nexus/registration_protocol.hpp>
#include <vector>

namespace nexus {
inline constexpr Result Unavailable = MAKERESULT(Module_Libnx, LibnxError_NotFound);
inline constexpr Result Invalid = MAKERESULT(Module_Libnx, LibnxError_BadInput);
struct Status {
    u8 mounted, ready, state, reserved;
    u32 result;
    u64 generation;
    bool Available() const { return mounted && ready && state == 4 && !result && generation; }
};
struct Info { u64 size; u32 cluster; u8 directory, padding[3]; };
struct Entry { char name[256]; u64 size; u8 directory, padding[7]; };
struct Space { u64 free, total; };
static_assert(sizeof(Status) == 16 && sizeof(Info) == 16 && sizeof(Entry) == 272 && sizeof(Space) == 16);
Provider Detect(Result &rc);
void Log(const char *phase, Result rc = 0, u64 value = 0);
// Cheap physical-SD journal existence check. Explicit repair remains available.
Result CheckUsbMutationAllowed();
Result CheckUsbFirmware();
// Caller must have verified the physical bytes before publishing registration.
Result EnsureUsbContentRegistered(u64 generation, const NcmContentId& id, s64 expected_size);

// Each client owns its own IPC session; the reader and installer may overlap.
// Neither acquires usb:hs or mounts a second filesystem on the same medium.
class Client {
    Service service{};
    size_t transfer_size = TransferSize;
public:
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client() { Close(); }
    void Close();
    size_t TransferLimit() const { return transfer_size; }
    Result Query(Status &status);
    Result ReportSnapshot(std::string& snapshot);
    Result Check(u64 generation);
    Result Stat(u64 generation, const std::string &path, Info &info);
    Result Read(u64 generation, const std::string &path, u64 offset, void *buf, size_t size, u64 &actual);
    Result Write(u64 generation, const std::string &path, u64 offset, const void *buf, size_t size);
    Result List(u64 generation, const std::string &path, std::vector<Entry> &entries);
    Result GetSpace(u64 generation, Space &space);
    Result CreateFile(u64 generation,const std::string& path,u64 size);
    Result CreateDirectory(u64 generation,const std::string& path);
    // Only an owned NSP dump's temporary folder/data file, never game content.
    Result DeleteDumpTemporary(u64 generation,const std::string& path);
    // Publishes a staged file without replacing an existing destination.
    Result RenameNew(u64 generation,const std::string& from,const std::string& to);
    Result Flush(u64 generation);
    Result VirtualSdStatus(NexusVsdStatus &status);
    Result EnsureVirtualSdLoader(NexusVsdStatus &status);
    Result ArmVirtualSd(u64 generation,const std::string &physical_nro);
    Result CancelVirtualSd();
    // Exact registered NCA only; no recursive or fallback-to-SD deletion.
    Result DeleteRegistered(u64 generation,const std::string& path,u64 expected_size);
    // Exact reviewed port file only, after destination readback and launcher creation.
    Result DeleteInstallTemporary(u64 generation,const std::string& path,u64 expected_size);
    Result DeletePortFile(u64 generation,const std::string& path,u64 expected_size);
    // Exact reviewed ordinary file or empty directory. Never recursive.
    Result DeleteFileManagerEntry(u64 generation,const std::string& path,u64 expected_size,bool directory,const std::string& reviewed_root);
};

inline NcmStorageId LogicalStorage(Target t) { return t == Target::Internal ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard; }
class Destination {
    Target target;
    u64 required_generation{};
    SdRoute sd_route = SdRoute::Unavailable;
    u64 generation = 0;
    bool active = false;
    Client client;
    Result Probe(NcmContentStorage &storage);
public:
    explicit Destination(Target t,u64 expected_generation=0): target(t),required_generation(expected_generation) {}
    Result Open(NcmContentStorage &storage);
    void End() { active = false; generation = 0; sd_route = SdRoute::Unavailable; client.Close(); }
    Result Validate();
    Result Write(NcmContentStorage &storage, const NcmPlaceHolderId &id, const std::string &path,
        u64 total, u64 offset, const void *buf, size_t size);
    Result Verify(NcmContentStorage &storage, const NcmContentId &id, u64 size);
    // Call only after a completed install hash or VerifyPair; USB only.
    Result EnsureRegistered(const NcmContentId &id, u64 size);
    bool IsUsb() const { return target == Target::Usb; }
    u64 Generation() const { return generation; }
    size_t TransferLimit() const { return IsUsb() ? client.TransferLimit() : TransferSize; }
};
// Non-mutating UI capability/space check. USB write routing is proven at install.
Result QueryDestination(Target target, Space &space);
// Raw SD root, not the USB-routed NCM content store. Never falls back to USB.
Result QueryPhysicalSd(Space &space);
}
