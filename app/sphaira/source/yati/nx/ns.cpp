#include "nexus/nx_Client.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/service_guard.h"
#include "defines.hpp"

namespace sphaira::ns {
namespace {

Service g_nsAppSrv;
bool g_ns_acquired{};
bool g_ns_child_owned{};

NX_GENERATE_SERVICE_GUARD(nsEx);

Result _nsExInitialize() {
    R_TRY(nsInitialize());
    g_ns_acquired = true;

    if (hosversionAtLeast(3,0,0)) {
        const auto opened=nsGetApplicationManagerInterface(&g_nsAppSrv);
        nexus::Log("install-open-application-interface",opened);
        R_TRY(opened);
        g_ns_child_owned = true;
    } else {
        g_nsAppSrv = *nsGetServiceSession_ApplicationManagerInterface();
    }

    R_SUCCEED();
}

void _nsExCleanup() {
    // ServiceGuard invokes this on failed init too. Never consume another
    // client's NS reference when our nsInitialize failed, or close the
    // pre-3.0 borrowed child twice (nsExit owns it in that case).
    if (g_ns_child_owned) serviceClose(&g_nsAppSrv);
    g_ns_child_owned = false;
    g_nsAppSrv = {};
    if (g_ns_acquired) nsExit();
    g_ns_acquired = false;
}

} // namespace

Result Initialize() {
    return nsExInitialize();
}

void Exit() {
    nsExExit();
}

Result PushApplicationRecord(u64 tid, const ncm::ContentStorageRecord* records, u32 count) {
    return PushApplicationRecordWithType(tid,records,count,ApplicationRecordType_Installed);
}
Result PushApplicationRecordWithType(u64 tid,const ncm::ContentStorageRecord* records,u32 count,u8 event) {
    const struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = { event, {0}, tid };

    return serviceDispatchIn(&g_nsAppSrv, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { records, sizeof(*records) * count } });
}

Result ListApplicationRecordContentMeta(u64 offset, u64 tid, ncm::ContentStorageRecord* out_records, u32 count, s32* entries_read) {
    struct {
        u64 offset;
        u64 tid;
    } in = { offset, tid };

    return serviceDispatchInOut(&g_nsAppSrv, 17, in, *entries_read,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out_records, sizeof(*out_records) * count } });
}

Result DeleteApplicationRecord(u64 tid) {
    return serviceDispatchIn(&g_nsAppSrv, 27, tid);
}

Result InvalidateApplicationControlCache(u64 tid) {
    return serviceDispatchIn(&g_nsAppSrv, 404, tid);
}

Result GetApplicationRecords(u64 id, std::vector<ncm::ContentStorageRecord>& out) {
    out.clear();
    s32 count{};
    const auto count_result = nsCountApplicationContentMeta(id, &count);
    if (R_FAILED(count_result)) {
        // A fresh title may not have an NS record yet. Do not treat arbitrary
        // service failures as an empty database: positively enumerate absence.
        NsApplicationRecord page[128]{};
        for(s32 offset=0;offset<65536;offset+=128) {
            s32 read{};
            R_TRY(nsListApplicationRecord(page,128,offset,&read));
            R_UNLESS(read>=0 && read<=128,MAKERESULT(Module_Libnx,LibnxError_BadInput));
            for(s32 i=0;i<read;++i) R_UNLESS(page[i].application_id!=id,count_result);
            if(read<128) R_SUCCEED();
        }
        return count_result;
    }
    R_UNLESS(count>=0 && count<=65536,MAKERESULT(Module_Libnx,LibnxError_BadInput));
    if(!count) R_SUCCEED();
    s32 records_read{};
    out.resize(count);
    R_TRY(ns::ListApplicationRecordContentMeta(0, id, out.data(), out.size(), &records_read));
    R_UNLESS(records_read==count,MAKERESULT(Module_Libnx,LibnxError_BadInput));
    out.resize(records_read);

    R_SUCCEED();
}

Result SetLowestLaunchVersion(u64 id) {
    std::vector<ncm::ContentStorageRecord> records;
    R_TRY(GetApplicationRecords(id, records));

    return SetLowestLaunchVersion(id, records);
}

Result SetLowestLaunchVersion(u64 id, std::span<const ncm::ContentStorageRecord> records) {
    R_TRY(avmInitialize());
    ON_SCOPE_EXIT(avmExit());

    u32 new_version = 0;
    for (const auto& record : records) {
        new_version = std::max(new_version, record.key.version);
    }

    return avmPushLaunchVersion(id, new_version);
}

} // namespace sphaira::ns
