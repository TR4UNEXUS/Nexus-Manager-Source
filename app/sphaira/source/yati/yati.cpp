#include "yati/yati.hpp"
#include "nexus/manager.hpp"
#include "nexus/package.hpp"
#include "yati/source/file.hpp"
#include "yati/source/stream_file.hpp"
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"
#include "msp.hpp"

#include "yati/nx/ncz.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/content_hash.hpp"
#include "nexus/content_compat_core.hpp"
#include "nexus/usb_ticket_store.hpp"
#include "nexus/usb_verification_proof.hpp"
#include "nexus/manager_update_core.hpp"
#include "nexus/release.hpp"

#include "utils/utils.hpp"
#include "utils/thread.hpp"

#include "ui/progress_box.hpp"

#include "app.hpp"
#include "nexus/home_badges.hpp"
#include "nexus/manager.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <zstd.h>
#include <minIni.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace sphaira::yati {
namespace {

constexpr NcmStorageId NCM_STORAGE_IDS[]{
    NcmStorageId_BuiltInUser,
    NcmStorageId_SdCard,
};

constexpr u32 KEYGEN_LIMIT = 0x20;

struct NcaCollection : container::CollectionEntry {
    nca::Header header{};
    // NcmContentType
    u8 type{};
    NcmContentId content_id{};
    NcmPlaceHolderId placeholder_id{};
    bool placeholder_owned{};
    std::string usb_path;
    // new hash of the nca..
    u8 hash[SHA256_HASH_SIZE]{};
    u8 expected_hash[SHA256_HASH_SIZE]{};
    u8 source_digest[SHA256_HASH_SIZE]{}; // matched source representation, before header edits
    u64 expected_size{};
    bool has_expected_hash{};
    bool converted_hash{};
    bool verified_alias{};
    nexus::install::HeaderSizeIssue header_size_issue{};
    // set true if nca has been modified.
    bool modified{};
    // set if the nca was not installed.
    bool skipped{};
    nexus::verification::Written usb_written{};
};

nexus::verification::Key VerificationKey(const NcaCollection& nca,u64 generation) {
    nexus::verification::Key key;
    memcpy(key.content.data(),&nca.content_id,key.content.size());
    key.expected_bytes=nca.has_expected_hash?32:16;
    memcpy(key.expected.data(),nca.has_expected_hash?static_cast<const void*>(nca.expected_hash):&nca.content_id,key.expected_bytes);
    key.generation=generation;key.size=nca.has_expected_hash?nca.expected_size:nca.header.size;
    key.plaintext_alias=nca.verified_alias;return key;
}

struct CnmtCollection : NcaCollection {
    // list of all nca's the cnmt depends on
    std::vector<NcaCollection> ncas{};
    // only set if any of the nca's depend on a ticket / cert.
    // if set, the ticket / cert will be installed once all nca's have installed.
    std::vector<FsRightsId> rights_id{};

    NcmContentMetaHeader meta_header{};
    NcmContentMetaKey key{};
    NcmContentInfo content_info{};
    std::vector<u8> extended_header{};
    std::vector<u8> metadata_tail{};
    std::vector<NcmPackagedContentInfo> infos{};
    u8 source_install_type{};
};

// Application family comes from CNMT extended metadata, including data patches.
u64 ApplicationId(const CnmtCollection& c) {
    if(c.key.type == NcmContentMetaType_Application) return c.key.id;
    if(c.key.type == NcmContentMetaType_DataPatch) {
        if(c.extended_header.size()<sizeof(NcmDataPatchMetaExtendedHeader))return 0;
        NcmDataPatchMetaExtendedHeader h{};memcpy(&h,c.extended_header.data(),sizeof(h));return h.application_id;
    }
    if(c.key.type == NcmContentMetaType_Patch || c.key.type == NcmContentMetaType_AddOnContent) {
        if(c.extended_header.size()<sizeof(u64))return 0;
        u64 id{};memcpy(&id,c.extended_header.data(),sizeof(id));return id;
    }
    return 0;
}

struct TikCollection {
    // raw data of the ticket / cert.
    std::vector<u8> ticket{};
    std::vector<u8> cert{};
    // set via the name of the ticket.
    FsRightsId rights_id{};
    // retrieved via the master key set in nca.
    u8 key_gen{};
    // set if ticket is required by an nca.
    bool required{};
    // set if ticket has already been patched.
    bool patched{};
};

struct Yati;

const u64 INFLATE_BUFFER_MAX = 1024*1024*4;

struct ThreadBuffer {
    ThreadBuffer() {
        buf.reserve(INFLATE_BUFFER_MAX);
    }

    std::vector<u8> buf;
    s64 off;
};

template<std::size_t Size>
struct RingBuf {
private:
    ThreadBuffer buf[Size]{};
    unsigned r_index{};
    unsigned w_index{};

    static_assert((sizeof(RingBuf::buf) & (sizeof(RingBuf::buf) - 1)) == 0, "Must be power of 2!");

public:
    void ringbuf_reset() {
        this->r_index = this->w_index;
    }

    unsigned ringbuf_capacity() const {
        return sizeof(this->buf) / sizeof(this->buf[0]);
    }

    unsigned ringbuf_size() const {
        return (this->w_index - this->r_index) % (ringbuf_capacity() * 2U);
    }

    unsigned ringbuf_free() const {
        return ringbuf_capacity() - ringbuf_size();
    }

    void ringbuf_push(std::vector<u8>& buf_in, s64 off_in) {
        auto& value = this->buf[this->w_index % ringbuf_capacity()];
        value.off = off_in;
        std::swap(value.buf, buf_in);

        this->w_index = (this->w_index + 1U) % (ringbuf_capacity() * 2U);
    }

    void ringbuf_pop(std::vector<u8>& buf_out, s64& off_out) {
        auto& value = this->buf[this->r_index % ringbuf_capacity()];
        off_out = value.off;
        std::swap(value.buf, buf_out);

        this->r_index = (this->r_index + 1U) % (ringbuf_capacity() * 2U);
    }
};

struct ThreadData {
    ThreadData(Yati* _yati, std::span<TikCollection> _tik, NcaCollection* _nca, bool is_usb_destination)
    : yati{_yati}, tik{_tik}, nca{_nca} {
        mutexInit(std::addressof(read_mutex));
        mutexInit(std::addressof(write_mutex));

        condvarInit(std::addressof(can_read));
        condvarInit(std::addressof(can_decompress));
        condvarInit(std::addressof(can_decompress_write));
        condvarInit(std::addressof(can_write));

        ueventCreate(&m_uevent_done, false);
        ueventCreate(&m_uevent_progres, true);

        sha256ContextCreate(&sha256);
        // this will be updated with the actual size from nca header.
        write_size = nca->size;

        // Keep SD file-based emuMMC writes small. USB uses the existing 4 MiB
        // pipeline batch regardless of where the running emuMMC is stored.
        if (!is_usb_destination && App::IsFileBaseEmummc()) {
            read_buffer_size = 1024 * 512;
        } else {
            read_buffer_size = 1024*1024*4;
        }

        max_buffer_size = std::max(read_buffer_size, INFLATE_BUFFER_MAX);
    }

    auto GetResults() volatile -> Result;
    void WakeAllThreads();

    auto IsAnyRunning() volatile const -> bool {
        return read_running || decompress_running || write_running;
    }

    auto GetWriteOffset() volatile const -> s64 {
        return write_offset;
    }

    auto GetWriteSize() volatile const -> s64 {
        return write_size;
    }

    auto GetDoneEvent() {
        return &m_uevent_done;
    }

    auto GetProgressEvent() {
        return &m_uevent_progres;
    }

    void SetReadResult(Result result) {
        read_result = result;

        // wake up decompress thread as it may be waiting on data that never comes.
        condvarWakeOne(std::addressof(can_decompress));

        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
    }

    void SetDecompressResult(Result result) {
        decompress_result = result;

        // wake up write thread as it may be waiting on data that never comes.
        condvarWakeOne(std::addressof(can_write));

        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
    }

    void SetWriteResult(Result result) {
        write_result = result;

        // wake up decompress thread as it may be waiting on data that never comes.
        condvarWakeOne(std::addressof(can_decompress_write));

        ueventSignal(GetDoneEvent());
    }

    Result Read(void* buf, s64 size, u64* bytes_read);

    Result SetDecompressBuf(std::vector<u8>& buf, s64 off, s64 size) {
        buf.resize(size);

        mutexLock(std::addressof(read_mutex));
        if (!read_buffers.ringbuf_free()) {
            if (!write_running) {
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_read), std::addressof(read_mutex)));
        }

        ON_SCOPE_EXIT(mutexUnlock(std::addressof(read_mutex)));
        R_TRY(GetResults());
        read_buffers.ringbuf_push(buf, off);
        return condvarWakeOne(std::addressof(can_decompress));
    }

    Result GetDecompressBuf(std::vector<u8>& buf_out, s64& off_out) {
        mutexLock(std::addressof(read_mutex));
        if (!read_buffers.ringbuf_size()) {
            if (!read_running) {
                buf_out.resize(0);
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_decompress), std::addressof(read_mutex)));
        }

        ON_SCOPE_EXIT(mutexUnlock(std::addressof(read_mutex)));
        R_TRY(GetResults());
        read_buffers.ringbuf_pop(buf_out, off_out);
        return condvarWakeOne(std::addressof(can_read));
    }

    Result SetWriteBuf(std::vector<u8>& buf, s64 size, bool skip_verify) {
        buf.resize(size);
        if (!skip_verify) {
            sha256ContextUpdate(std::addressof(sha256), buf.data(), buf.size());
        }

        mutexLock(std::addressof(write_mutex));
        if (!write_buffers.ringbuf_free()) {
            if (!decompress_running) {
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_decompress_write), std::addressof(write_mutex)));
        }

        ON_SCOPE_EXIT(mutexUnlock(std::addressof(write_mutex)));
        R_TRY(GetResults());
        write_buffers.ringbuf_push(buf, 0);
        return condvarWakeOne(std::addressof(can_write));
    }

    Result GetWriteBuf(std::vector<u8>& buf_out, s64& off_out) {
        mutexLock(std::addressof(write_mutex));
        if (!write_buffers.ringbuf_size()) {
            if (!decompress_running) {
                buf_out.resize(0);
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_write), std::addressof(write_mutex)));
        }

        ON_SCOPE_EXIT(mutexUnlock(std::addressof(write_mutex)));
        R_TRY(GetResults());
        write_buffers.ringbuf_pop(buf_out, off_out);
        return condvarWakeOne(std::addressof(can_decompress_write));
    }

    // these need to be copied
    Yati* yati{};
    std::span<TikCollection> tik{};
    NcaCollection* nca{};

    // these need to be created
    Mutex read_mutex{};
    Mutex write_mutex{};

    CondVar can_read{};
    CondVar can_decompress{};
    CondVar can_decompress_write{};
    CondVar can_write{};

    UEvent m_uevent_done{};
    UEvent m_uevent_progres{};

    RingBuf<4> read_buffers{};
    RingBuf<4> write_buffers{};

    ncz::BlockHeader ncz_block_header{};
    std::vector<ncz::Section> ncz_sections{};
    std::vector<ncz::BlockInfo> ncz_blocks{};

    Sha256Context sha256{};
    nca::ContentHash source_hash;

    u64 read_buffer_size{};
    u64 max_buffer_size{};

    // these are shared between threads
    std::atomic<s64> read_offset{};
    std::atomic<s64> decompress_offset{};
    std::atomic<s64> write_offset{};
    std::atomic<s64> write_size{};

    std::atomic<Result> read_result{};
    std::atomic<Result> decompress_result{};
    std::atomic<Result> write_result{};

    std::atomic_bool read_running{true};
    std::atomic_bool decompress_running{true};
    std::atomic_bool write_running{true};
};

struct UsbVerificationProgress {
    u64 total_bytes{}, completed_bytes{};
    size_t total_files{}, completed_files{};
};

struct Yati {
    Yati(ui::ProgressBox*, source::Base*);
    ~Yati();

    Result Setup(const ConfigOverride& override);
    Result PublishNca(const NcaCollection& nca);
    Result PublishNcas(const CnmtCollection& cnmt);
    Result VerifyReusedNca(const NcaCollection& nca,bool allow_written=false,const u8* exact_output_hash=nullptr,Result exact_hash_error=Result_YatiInvalidNcaSha256);
    Result Checkpoint(nexusapp::InstallStep step) {
        if(!receipt)return 0;
        receipt->step=step;receipt->title=diagnostic_title;
        return nexusapp::SaveInstallReceipt(*receipt);
    }
    void Cleanup(const NcaCollection& nca) {
        if(nca.placeholder_owned && destination && R_SUCCEEDED(destination->Validate()))
            ncmContentStorageDeletePlaceHolder(&cs,&nca.placeholder_id);
    }
    Result InstallNca(std::span<TikCollection> tickets, NcaCollection& nca);
    Result InstallNcaInternal(std::span<TikCollection> tickets, NcaCollection& nca);
    Result InstallCnmtNca(std::span<TikCollection> tickets, CnmtCollection& cnmt, const container::Collections& collections);
    Result FindContent(const NcmPackagedContentInfo&, u64 owner, const container::Collections&, const container::CollectionEntry*&);
    Result ReadSourceExact(void* data, s64 offset, s64 size, Result invalid_read = Result_YatiInvalidNcaReadSize);

    Result readFuncInternal(ThreadData* t);
    Result decompressFuncInternal(ThreadData* t);
    Result writeFuncInternal(ThreadData* t);

    Result ParseTicketsIntoCollection(std::vector<TikCollection>& tickets, const container::Collections& collections, bool read_data);
    Result GetLatestVersion(const CnmtCollection& cnmt, u32& version_out, bool& skip);
    Result ShouldSkip(const CnmtCollection& cnmt, bool& skip);
    Result ImportTickets(std::span<TikCollection> tickets);
    Result RemoveInstalledNcas(const CnmtCollection& cnmt);
    Result RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num);
    bool defer_postcommit{};
    bool archive_usb_tickets{true};


// private:
    ui::ProgressBox* pbox{};
    source::Base* source{};

    // for all content storages
    NcmContentStorage ncm_cs[2]{};
    NcmContentMetaDatabase ncm_db[2]{};
    // these point to the above struct
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    NcmStorageId storage_id{};
    nexus::Target physical_target{};
    std::unique_ptr<nexus::Destination> destination;
    u64 usb_generation{};
    UsbVerificationProgress* usb_verification{};
    bool quick_usb_final_verify{};
    nexus::verification::Cache usb_verified{};
    u64 alias_bytes_left{256ULL*1024*1024};
    size_t alias_candidates_left{64};

    std::unique_ptr<container::Base> container{};
    Config config{};
    nexus::install::Diagnostic* diagnostic{};
    InstallOutcome* outcome{};
    nexusapp::InstallReceipt* receipt{};
    u64 diagnostic_title{};
    keys::Keys keys{};

    // tracks what Setup() actually managed to acquire. these services are
    // refcounted process wide and other parts of sphaira (the game menu, the
    // game:/ devoptab, ...) hold them at the same time, so releasing one that
    // we never acquired would close the session out from under them.
    bool spl_crypto_init{};
    bool ns_init{};
    bool es_init{};
};

auto ThreadData::GetResults() volatile -> Result {
    R_TRY(yati->pbox->ShouldExitResult());
    R_TRY(read_result.load());
    R_TRY(decompress_result.load());
    R_TRY(write_result.load());
    R_SUCCEED();
}

void ThreadData::WakeAllThreads() {
    condvarWakeAll(std::addressof(can_read));
    condvarWakeAll(std::addressof(can_decompress));
    condvarWakeAll(std::addressof(can_decompress_write));
    condvarWakeAll(std::addressof(can_write));

    mutexUnlock(std::addressof(read_mutex));
    mutexUnlock(std::addressof(write_mutex));
}

Result ThreadData::Read(void* buf, s64 size, u64* bytes_read) {
    *bytes_read = 0;
    const s64 start = read_offset.load();
    R_UNLESS(size >= 0 && start >= 0 && nca->size >= start && nca->offset >= 0
        && nca->offset <= INT64_MAX - nca->size, Result_YatiInvalidNcaReadSize);
    size = std::min<s64>(size, nca->size - start);
    // C8: a successful short read is progress, not corrupt content. Fill the
    // bounded request at the next offset (also works on forward-only streams).
    // Never retry an error or zero progress; cancellation/media checks stay live.
    while (*bytes_read < u64(size)) {
        R_TRY(GetResults());
        R_TRY(yati->destination->Validate());
        const auto remaining = u64(size) - *bytes_read;
        u64 got{};
        R_TRY(yati->source->Read(static_cast<u8*>(buf) + *bytes_read,
            nca->offset + read_offset.load(), remaining, &got));
        R_UNLESS(got && got <= remaining, Result_YatiInvalidNcaReadSize);
        read_offset += got;
        *bytes_read += got;
    }
    R_SUCCEED();
}

auto GetTicketCollection(const nca::Header& header, std::span<TikCollection> tik) -> TikCollection* {
    TikCollection* ticket{};

    if (es::IsRightsIdValid(header.rights_id)) {
        auto it = std::ranges::find_if(tik, [&header](auto& e){
            return !std::memcmp(&header.rights_id, &e.rights_id, sizeof(e.rights_id));
        });

        if (it != tik.end()) {
            it->required = true;
            it->key_gen = header.GetKeyGeneration();
            ticket = &(*it);
        }
    }

    return ticket;
}

Result HasRequiredTicket(const nca::Header& header, TikCollection* ticket) {
    if (es::IsRightsIdValid(header.rights_id)) {
        log_write("looking for ticket %s\n", utils::hexIdToStr(header.rights_id).str);
        R_UNLESS(ticket, Result_YatiTicketNotFound);
        log_write("ticket found\n");
    }
    R_SUCCEED();
}

Result HasRequiredTicket(const nca::Header& header, std::span<TikCollection> tik) {
    auto ticket = GetTicketCollection(header, tik);
    return HasRequiredTicket(header, ticket);
}

// read thread reads all data from the source, it also handles
// parsing ncz headers, sections and reading ncz blocks
Result Yati::readFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->read_running = false; );

    // the main buffer which data is read into.
    std::vector<u8> buf;
    // workaround ncz block reading ahead. if block isn't found, we usually
    // would seek back to the offset, however this is not possible in stream
    // mode, so we instead store the data to the temp buffer and pre-pend it.
    std::vector<u8> temp_buf;
    buf.reserve(t->max_buffer_size);
    temp_buf.reserve(t->max_buffer_size);

    while (t->read_offset < t->nca->size && R_SUCCEEDED(t->GetResults())) {
        const auto buffer_offset = t->read_offset.load();

        // read more data
        s64 read_size = t->read_buffer_size;
        if (!t->read_offset) {
            read_size = NCZ_SECTION_OFFSET;
        }

        s64 buf_offset = 0;
        if (!temp_buf.empty()) {
            buf = temp_buf;
            read_size -= temp_buf.size();
            buf_offset = temp_buf.size();
            temp_buf.clear();
        }

        u64 bytes_read{};
        buf.resize(buf_offset + read_size);
        R_TRY(t->Read(buf.data() + buf_offset, read_size, std::addressof(bytes_read)));
        auto buf_size = buf_offset + bytes_read;
        if (!bytes_read) {
            break;
        }

        // read enough bytes for ncz, check magic
        if (t->read_offset == NCZ_SECTION_OFFSET) {
            // check for ncz section header.
            ncz::Header header{};
            std::memcpy(std::addressof(header), buf.data() + 0x4000, sizeof(header));
            if (header.magic == NCZ_SECTION_MAGIC) {
                // validate section header.
                R_UNLESS(nexus::compat::TableFits(header.total_sections, sizeof(ncz::Section),
                    t->nca->size-t->read_offset.load(), nexus::compat::MaxSections), Result_YatiInvalidNczSectionCount);

                buf_size = 0x4000;
                log_write("found ncz, total number of sections: %zu\n", header.total_sections);
                t->ncz_sections.resize(header.total_sections);
                R_TRY(t->Read(t->ncz_sections.data(), t->ncz_sections.size() * sizeof(ncz::Section), std::addressof(bytes_read)));
                R_UNLESS(bytes_read == t->ncz_sections.size()*sizeof(ncz::Section), Result_YatiInvalidNczSectionCount);

                // check for ncz block header.
                R_TRY(t->Read(std::addressof(t->ncz_block_header.magic), sizeof(u64), std::addressof(bytes_read)));
                R_UNLESS(bytes_read == sizeof(u64), Result_YatiInvalidNczBlockTotal);
                if (t->ncz_block_header.magic != NCZ_BLOCK_MAGIC) {
                    // didn't find block, keep the data we just read in the temp buffer.
                    temp_buf.resize(sizeof(u64));
                    std::memcpy(temp_buf.data(), std::addressof(t->ncz_block_header), temp_buf.size());
                    log_write("storing temp data of size: %zu\n", temp_buf.size());
                } else {
                    R_TRY(t->Read(reinterpret_cast<u8*>(&t->ncz_block_header)+sizeof(u64), sizeof(t->ncz_block_header)-sizeof(u64), &bytes_read));
                    R_UNLESS(bytes_read==sizeof(t->ncz_block_header)-sizeof(u64), Result_YatiInvalidNczBlockTotal);
                    // validate block header.
                    R_TRY(t->ncz_block_header.IsValid());
                    R_UNLESS(nexus::compat::TableFits(t->ncz_block_header.total_blocks, sizeof(ncz::Block),
                        t->nca->size-t->read_offset.load(), nexus::compat::MaxBlocks), Result_YatiInvalidNczBlockTotal);
                    const u64 block_size = 1ULL << t->ncz_block_header.block_size_exponent;
                    const u64 decompressed = t->ncz_block_header.decompressed_size;
                    R_UNLESS(decompressed && (decompressed-1)/block_size+1 == t->ncz_block_header.total_blocks,
                        Result_YatiInvalidNczBlockTotal);

                    // read blocks (array of block sizes).
                    std::vector<ncz::Block> blocks(t->ncz_block_header.total_blocks);
                    R_TRY(t->Read(blocks.data(), blocks.size() * sizeof(ncz::Block), std::addressof(bytes_read)));
                    R_UNLESS(bytes_read == blocks.size()*sizeof(ncz::Block), Result_YatiInvalidNczBlockTotal);

                    // calculate offsets for each block.
                    auto block_offset = t->read_offset.load();
                    for (const auto& block : blocks) {
                        const u64 index=t->ncz_blocks.size();
                        const u64 expected=std::min(block_size,decompressed-index*block_size);
                        R_UNLESS(block.size && block.size<=expected && block.size<=u64(t->nca->size-block_offset),
                            Result_YatiInvalidNczBlockTotal);
                        t->ncz_blocks.emplace_back(block_offset, block.size);
                        block_offset += block.size;
                    }
                    R_UNLESS(block_offset==t->nca->size, Result_YatiInvalidNczBlockTotal);
                }
            }
        }

        R_TRY(t->SetDecompressBuf(buf, buffer_offset, buf_size));
    }

    log_write("read success\n");
    R_SUCCEED();
}

// decompress thread handles decrypting / modifying the nca header, decompressing ncz
// and calculating the running sha256.
Result Yati::decompressFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->decompress_running = false; );

    // only used for ncz files.
    auto dctx = ZSTD_createDCtx();
    ON_SCOPE_EXIT(ZSTD_freeDCtx(dctx));
    const auto chunk_size = ZSTD_DStreamOutSize();
    const ncz::Section* ncz_section{};
    const ncz::BlockInfo* ncz_block{};
    size_t section_cursor{}, block_cursor{};
    size_t zstd_remaining{};
    bool is_ncz{};

    s64 inflate_offset{};
    Aes128CtrContext ctx{};
    std::vector<u8> inflate_buf{};
    inflate_buf.reserve(t->max_buffer_size);

    s64 written{};
    s64 block_offset{};
    std::vector<u8> buf{};
    buf.reserve(t->max_buffer_size);

    // encrypts the nca and passes the buffer to the write thread.
    const auto ncz_flush = [&](s64 size) -> Result {
        if (!inflate_offset) {
            R_SUCCEED();
        }

        // if we are not moving the whole vector, then we need to keep
        // the remaining data.
        // rather that copying the entire vector to the write thread,
        // only copy (store) the remaining amount.
        std::vector<u8> temp_vector{};
        if (size < inflate_offset) {
            temp_vector.resize(inflate_offset - size);
            std::memcpy(temp_vector.data(), inflate_buf.data() + size, temp_vector.size());
        }

        for (s64 off = 0; off < size;) {
            if (!ncz_section || !ncz_section->InRange(written)) {
                ncz_section = nexus::compat::FindForward(t->ncz_sections, section_cursor, written);
                if (!ncz_section && section_cursor<t->ncz_sections.size() && u64(written)<t->ncz_sections[section_cursor].offset) {
                    // Bytes outside encrypted sections are stored verbatim, including
                    // a leading gap after the raw 0x4000-byte NCA prefix.
                    const auto gap=std::min<u64>(t->ncz_sections[section_cursor].offset-written,size-off);
                    written+=gap; off+=gap; continue;
                }
                R_UNLESS(ncz_section, Result_YatiNczSectionNotFound);

                if (ncz_section->crypto_type >= nca::EncryptionType_AesCtr) {
                    const auto swp = std::byteswap(u64(written) >> 4);
                    u8 counter[0x10];
                    std::memcpy(counter + 0x0, ncz_section->counter, 0x8);
                    std::memcpy(counter + 0x8, &swp, 0x8);
                    aes128CtrContextCreate(&ctx, ncz_section->key, counter);
                }
            }

            const auto total_size = ncz_section->offset + ncz_section->size;
            const auto chunk_size = std::min<u64>(total_size - written, size - off);

            if (ncz_section->crypto_type >= nca::EncryptionType_AesCtr) {
                aes128CtrCrypt(&ctx, inflate_buf.data() + off, inflate_buf.data() + off, chunk_size);
            }

            written += chunk_size;
            off += chunk_size;
        }

        R_UNLESS(written<=t->write_size, Result_YatiInvalidNczZstdError);
        t->source_hash.Update(inflate_buf.data(), size);
        R_TRY(t->SetWriteBuf(inflate_buf, size, false));
        inflate_offset -= size;

        // restore remaining data to the swapped buffer.
        if (!temp_vector.empty()) {
            log_write("[NCZ] storing data size: %zu\n", temp_vector.size());
            inflate_buf = temp_vector;
        }

        R_SUCCEED();
    };

    while ((is_ncz || t->decompress_offset < t->write_size) && R_SUCCEEDED(t->GetResults())) {
        s64 decompress_buf_off{};
        R_TRY(t->GetDecompressBuf(buf, decompress_buf_off));
        if (buf.empty()) {
            break;
        }

        // do we have an nsz? if so, setup buffers.
        if (!is_ncz && !t->ncz_sections.empty()) {
            log_write("YES IT FOUND NCZ\n");
            is_ncz = true;
        }

        // if we don't have a ncz or it's before the ncz header, pass buffer directly to write
        if (!is_ncz || !decompress_buf_off) {
            // Preserve the digest of source bytes before any installer edits.
            t->source_hash.Update(buf.data(), buf.size(), keys.header_key);
            // check nca header
            if (!decompress_buf_off) {
                log_write("reading nca header\n");

                log_write("verifying nca header magic\n");
                nca::Header header{};
                R_UNLESS(buf.size()>=sizeof(header), Result_YatiInvalidNcaSha256);
                // Check the entire raw header, not only its signature or magic.
                // Facts are reported after the worker joins; never log bytes/keys.
                if (std::all_of(buf.begin(),buf.begin()+sizeof(header),[](u8 value){return value==0;})) {
                    t->nca->header_size_issue=nexus::install::HeaderSizeIssue::Empty;
                    R_UNLESS(false, Result_YatiInvalidNcaSha256);
                }
                const bool plaintext_alias=t->nca->verified_alias && !memcmp(buf.data()+0x200,"NCA3",4);
                if (plaintext_alias) {
                    memcpy(&header,buf.data(),sizeof(header));
                    R_UNLESS(header.content_type==nca::ContentType_Control && header.distribution_type==nca::DistributionType_System &&
                        header.size<=16*1024*1024 && !config.convert_to_standard_crypto && !config.lower_master_key,Result_YatiInvalidNcaSha256);
                    t->nca->modified=true; // encrypt the validated plaintext header for storage
                } else {
                    const auto header_rc=nca::DecryptHeader(buf.data(), keys, header);
                    if (!R_SUCCEEDED(header_rc))
                        t->nca->header_size_issue=nexus::install::HeaderSizeIssue::Undecodable;
                    R_TRY(header_rc);
                }
                log_write("nca magic is ok! type: %u\n", header.content_type);

                // store the unmodified header.
                t->nca->header = header;

                if (!config.skip_rsa_header_fixed_key_verify) {
                    log_write("verifying nca fixed key\n");
                    R_TRY(nca::VerifyFixedKey(header));
                    log_write("nca fixed key is ok! type: %u\n", header.content_type);
                } else {
                    log_write("skipping nca verification\n");
                }

                // Keep worker-owned facts until all threads join; the UI
                // diagnostic is populated on the caller thread, never here.
                t->nca->header_size_issue=nexus::install::CheckHeaderSize(header.size,
                    t->nca->size,is_ncz,t->nca->has_expected_hash,t->nca->expected_size);
                R_UNLESS(t->nca->header_size_issue==nexus::install::HeaderSizeIssue::None, Result_YatiInvalidNcaSha256);
                t->write_size = header.size;
                static constexpr const char* kinds[]={"Program","Content metadata","Control / icon","Manual","Data","Public data"};
                pbox->ContentFile(t->nca->name,header.content_type<std::size(kinds)?kinds[header.content_type]:"NCA","Writing NCA",header.size,t->nca->size);
                if (is_ncz) {
                    R_UNLESS(nexus::compat::ValidSections(t->ncz_sections,header.size), Result_YatiInvalidNczSectionCount);
                    if (t->ncz_sections.front().offset<0x4000)
                        log_write("C4: NCZ section overlaps preserved prefix; reconstruction starts at 0x4000\n");
                    if (!t->ncz_blocks.empty()) R_UNLESS(t->ncz_block_header.decompressed_size==header.size-0x4000, Result_YatiInvalidNczBlockTotal);
                }
                log_write("setting placeholder size: %zu\n", t->write_size.load());
                R_TRY(ncmContentStorageSetPlaceHolderSize(std::addressof(cs), std::addressof(t->nca->placeholder_id), t->write_size));

                if (header.distribution_type == nca::DistributionType_GameCard) {
                    header.distribution_type = nca::DistributionType_System;
                    t->nca->modified = true;
                }

                // try and get the ticket, if the nca requires it.
                auto ticket = GetTicketCollection(header, t->tik);
                R_TRY(HasRequiredTicket(header, ticket));

                if ((config.convert_to_standard_crypto && ticket) || config.lower_master_key) {
                    t->nca->modified = true;
                    u8 keak_generation = 0;

                    if (ticket) {
                        const auto key_gen = header.GetKeyGeneration();
                        log_write("converting to standard crypto: 0x%X 0x%X\n", key_gen, header.GetKeyGeneration());

                        keys::KeyEntry title_key;
                        R_TRY(es::GetTitleKeyDecrypted(ticket->ticket, header.rights_id, key_gen, keys, title_key));

                        std::memset(header.key_area, 0, sizeof(header.key_area));
                        std::memcpy(&header.key_area[0x2], &title_key, sizeof(title_key));

                        keak_generation = key_gen;
                        ticket->required = false;
                    } else if (config.lower_master_key) {
                        R_TRY(nca::DecryptKeak(keys, header));
                    }

                    R_TRY(nca::EncryptKeak(keys, header, keak_generation));
                    std::memset(&header.rights_id, 0, sizeof(header.rights_id));
                }

                if (t->nca->modified) {
                    crypto::cryptoAes128Xts(std::addressof(header), buf.data(), keys.header_key, 0, 0x200, sizeof(header), true);
                }
                // Fingerprint the exact bytes sent to USB, after supported
                // header edits (including plaintext Control aliases).
                sha256CalculateHash(t->nca->usb_written.header.data(),buf.data(),sizeof(header));
                t->nca->usb_written.header_known=true;
            }

            written += buf.size();
            t->decompress_offset += buf.size();
            R_TRY(t->SetWriteBuf(buf, buf.size(), false));
        } else if (is_ncz) {
            u64 buf_off{};
            while (buf_off < buf.size()) {
                std::span<const u8> buffer{buf.data() + buf_off, buf.size() - buf_off};
                bool compressed = true;

                // todo: blocks need to use read offset, as the offset + size is compressed range.
                if (t->ncz_blocks.size()) {
                    if (!ncz_block || !ncz_block->InRange(decompress_buf_off)) {
                        block_offset = 0;
                        R_UNLESS(!zstd_remaining, Result_YatiInvalidNczZstdError);
                        ncz_block = nexus::compat::FindForward(t->ncz_blocks, block_cursor, decompress_buf_off);
                        R_UNLESS(ncz_block, Result_YatiNczBlockNotFound);
                    }

                    // https://github.com/nicoboss/nsz/issues/79
                    auto decompressedBlockSize = 1UL << t->ncz_block_header.block_size_exponent;
                    // special handling for the last block to check it's actually compressed
                    if (ncz_block->offset == t->ncz_blocks.back().offset) {
                        log_write("[NCZ] last block special handling\n");
                        // https://github.com/nicoboss/nsz/issues/210
                        const auto remainder = t->ncz_block_header.decompressed_size % decompressedBlockSize;
                        if (remainder) {
                            decompressedBlockSize = remainder;
                        }
                    }

                    // check if this block is compressed.
                    compressed = ncz_block->size < decompressedBlockSize;

                    // clip read size as blocks can be up to 32GB in size!
                    const auto size = std::min<u64>(buffer.size(), ncz_block->size - block_offset);
                    buffer = buffer.subspan(0, size);
                }

                if (compressed) {
                    log_write("[NCZ] COMPRESSED block\n");
                    ZSTD_inBuffer input = { buffer.data(), buffer.size(), 0 };
                    while (input.pos < input.size) {
                        R_TRY(t->GetResults());

                        inflate_buf.resize(inflate_offset + chunk_size);
                        ZSTD_outBuffer output = { inflate_buf.data() + inflate_offset, chunk_size, 0 };
                        const auto res = ZSTD_decompressStream(dctx, std::addressof(output), std::addressof(input));
                        if (ZSTD_isError(res)) {
                            log_write("[NCZ] ZSTD_decompressStream() pos: %zu size: %zu res: %zd msg: %s\n", input.pos, input.size, res, ZSTD_getErrorName(res));
                        }
                        R_UNLESS(!ZSTD_isError(res), Result_YatiInvalidNczZstdError);
                        zstd_remaining=res;

                        t->decompress_offset += output.pos;
                        R_UNLESS(t->decompress_offset<=t->write_size, Result_YatiInvalidNczZstdError);
                        inflate_offset += output.pos;
                        if (inflate_offset >= INFLATE_BUFFER_MAX) {
                            log_write("[NCZ] flushing compressed data: %zd vs %zd diff: %zd\n", inflate_offset, INFLATE_BUFFER_MAX, inflate_offset - INFLATE_BUFFER_MAX);
                            R_TRY(ncz_flush(INFLATE_BUFFER_MAX));
                        }
                    }
                } else {
                    inflate_buf.resize(inflate_offset + buffer.size());
                    std::memcpy(inflate_buf.data() + inflate_offset, buffer.data(), buffer.size());

                    t->decompress_offset += buffer.size();
                    R_UNLESS(t->decompress_offset<=t->write_size, Result_YatiInvalidNczZstdError);
                    inflate_offset += buffer.size();
                    if (inflate_offset >= INFLATE_BUFFER_MAX) {
                        log_write("[NCZ] flushing copy data\n");
                        R_TRY(ncz_flush(INFLATE_BUFFER_MAX));
                    }
                }

                buf_off += buffer.size();
                decompress_buf_off += buffer.size();
                block_offset += buffer.size();
            }
        }
    }

    // flush remaining data.
    if (is_ncz && inflate_offset) {
        log_write("flushing remaining\n");
        R_TRY(ncz_flush(inflate_offset));
    }

    log_write("decompress thread done!\n");

    // get final hash output.
    R_UNLESS(written==t->write_size && !zstd_remaining, Result_YatiInvalidNcaSha256);
    sha256ContextGetHash(std::addressof(t->sha256), t->nca->hash);

    R_SUCCEED();
}

// write thread writes data to the nca placeholder.
Result Yati::writeFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->write_running = false; );

    std::vector<u8> buf;
    buf.reserve(t->max_buffer_size);
    const auto is_file_based_emummc = !destination->IsUsb() && App::IsFileBaseEmummc();

    while (t->write_offset < t->write_size && R_SUCCEEDED(t->GetResults())) {
        s64 dummy_off;
        R_TRY(t->GetWriteBuf(buf, dummy_off));
        if (buf.empty()) {
            break;
        }

        s64 off{};
        while (off < buf.size() && t->write_offset < t->write_size && R_SUCCEEDED(t->GetResults())) {
            const auto wsize = std::min<s64>(t->read_buffer_size, buf.size() - off);
            R_TRY(destination->Write(cs,t->nca->placeholder_id,t->nca->usb_path,t->write_size,t->write_offset,buf.data()+off,wsize));

            pbox->AddTransferBytes(wsize);
            off += wsize;
            t->write_offset += wsize;
            ueventSignal(t->GetProgressEvent());

            // todo: check how much time elapsed and sleep the diff
            // rather than always sleeping a fixed amount.
            // ie, writing a small buffer (nca header) should not sleep the full 2 ms.
            if (is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }
        }
    }

    log_write("finished write thread!\n");
    R_SUCCEED();
}

void readFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetReadResult(t->yati->readFuncInternal(t));
    log_write("read thread returned now\n");
}

void decompressFunc(void* d) {
    log_write("hello decomp thread func\n");
    auto t = static_cast<ThreadData*>(d);
    t->SetDecompressResult(t->yati->decompressFuncInternal(t));
    log_write("decompress thread returned now\n");
}

void writeFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetWriteResult(t->yati->writeFuncInternal(t));
    log_write("write thread returned now\n");
}

// stdio-like wrapper for std::vector
struct BufHelper {
    BufHelper() = default;
    BufHelper(std::span<const u8> data) {
        write(data);
    }

    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) {
            buf.resize(offset + size);
        }
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }

    void write(std::span<const u8> data) {
        write(data.data(), data.size());
    }

    void seek(u64 where_to) {
        offset = where_to;
    }

    [[nodiscard]]
    auto tell() const {
        return offset;
    }

    std::vector<u8> buf{};
    u64 offset{};
};

Yati::Yati(ui::ProgressBox* _pbox, source::Base* _source) : pbox{_pbox}, source{_source} {
    App::SetAutoSleepDisabled(true);
}

Yati::~Yati() {
    // released in reverse order of acquisition, and only what Setup() got as
    // far as acquiring, see the comment on the flags.
    // note: the ncm handles below don't need the same treatment, they are
    // zero initialised and closing an unopened one is a no-op.
    if (es_init) {
        es::Exit();
    }

    if (ns_init) {
        ns::Exit();
    }

    if (spl_crypto_init) {
        splCryptoExit();
    }

    for (size_t i = 0; i < std::size(NCM_STORAGE_IDS); i++) {
        ncmContentMetaDatabaseClose(std::addressof(ncm_db[i]));
        ncmContentStorageClose(std::addressof(ncm_cs[i]));
    }

    App::SetAutoSleepDisabled(false);

    // USB Loader rescans physical storage when Installed is opened/refreshed.
    // The upstream logical-SD game menu is not part of this app.
}

Result Yati::Setup(const ConfigOverride& override) {
    diagnostic=override.diagnostic;
    outcome=override.outcome;
    receipt=override.receipt;
    archive_usb_tickets=override.archive_usb_tickets.value_or(true);
    config.sd_card_install = override.sd_card_install.value_or(App::GetApp()->m_install_sd.Get());
    config.allow_downgrade = App::GetApp()->m_allow_downgrade.Get();
    config.skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    config.ticket_only = override.ticket_only.value_or(App::GetApp()->m_ticket_only.Get());
    config.skip_base = override.skip_base.value_or(App::GetApp()->m_skip_base.Get());
    config.skip_patch = override.skip_patch.value_or(App::GetApp()->m_skip_patch.Get());
    config.skip_addon = override.skip_addon.value_or(App::GetApp()->m_skip_addon.Get());
    config.skip_data_patch = override.skip_data_patch.value_or(App::GetApp()->m_skip_data_patch.Get());
    config.skip_ticket = override.skip_ticket.value_or(App::GetApp()->m_skip_ticket.Get());
    config.skip_nca_hash_verify = override.skip_nca_hash_verify.value_or(App::GetApp()->m_skip_nca_hash_verify.Get());
    config.skip_rsa_header_fixed_key_verify = override.skip_rsa_header_fixed_key_verify.value_or(App::GetApp()->m_skip_rsa_header_fixed_key_verify.Get());
    config.skip_rsa_npdm_fixed_key_verify = override.skip_rsa_npdm_fixed_key_verify.value_or(App::GetApp()->m_skip_rsa_npdm_fixed_key_verify.Get());
    config.ignore_distribution_bit = override.ignore_distribution_bit.value_or(App::GetApp()->m_ignore_distribution_bit.Get());
    config.convert_to_common_ticket = override.convert_to_common_ticket.value_or(App::GetApp()->m_convert_to_common_ticket.Get());
    config.convert_to_standard_crypto = override.convert_to_standard_crypto.value_or(App::GetApp()->m_convert_to_standard_crypto.Get());
    config.lower_master_key = override.lower_master_key.value_or(App::GetApp()->m_lower_master_key.Get());
    config.lower_system_version = override.lower_system_version.value_or(App::GetApp()->m_lower_system_version.Get());
    physical_target = override.nexus_target.value_or(config.sd_card_install ? nexus::Target::Sd : nexus::Target::Internal);
    quick_usb_final_verify=override.quick_usb_final_verify&&physical_target==nexus::Target::Usb;
    config.sd_card_install = physical_target != nexus::Target::Internal;
    config.skip_if_already_installed = false; // logical SD presence is not USB proof
    if(physical_target==nexus::Target::Usb) {
        config.skip_nca_hash_verify=false;
        // Retain the source crypto representation so destination read-back
        // can be verified against its original CNMT, including on resume.
        config.convert_to_standard_crypto=false;
        config.lower_master_key=false;
    }
    config.allow_downgrade = false;
    storage_id = nexus::LogicalStorage(physical_target);
    destination = std::make_unique<nexus::Destination>(physical_target,override.nexus_destination_generation);

    R_TRY(source->GetOpenResult());

    R_TRY(splCryptoInitialize());
    spl_crypto_init = true;

    R_TRY(ns::Initialize());
    ns_init = true;

    R_TRY(es::Initialize());
    es_init = true;

    for (size_t i = 0; i < std::size(NCM_STORAGE_IDS); i++) {
        R_TRY(ncmOpenContentMetaDatabase(std::addressof(ncm_db[i]), NCM_STORAGE_IDS[i]));
        if(i == static_cast<size_t>(config.sd_card_install)) {
            const auto opened=destination->Open(ncm_cs[i]);
            if(R_FAILED(opened) && diagnostic && destination->IsUsb())diagnostic->text=
                "USB preflight failed before content copying. P3 firmware and a ready USB drive are required. Install the matching package3 and fully reboot; keep the source. See nexus.log for the precise stage.";
            R_TRY(opened);
        } else {
            R_TRY(nexusapp::OpenStorage(i ? nexus::Target::Sd : nexus::Target::Internal,ncm_cs[i]));
        }
    }

    cs = ncm_cs[config.sd_card_install];
    db = ncm_db[config.sd_card_install];
    if(destination->IsUsb())usb_generation=destination->Generation();

    R_TRY(parse_keys(keys, true));
    R_SUCCEED();
}

Result Yati::InstallNcaInternal(std::span<TikCollection> tickets, NcaCollection& nca) {
    nca.usb_written={};
    if(destination->IsUsb() && !source->IsStream()) {
        // An interrupted attempt may have committed this NCA already. Inspect
        // physical USB, not routed Has(), and rehash it before any reuse.
        nexus::Client usb;R_TRY(usb.Check(usb_generation));
        char common[FS_MAX_PATH]{};std::string path;
        R_TRY(ncmContentStorageGetPath(&cs,common,sizeof(common),&nca.content_id));
        R_UNLESS(nexus::ContentPath(common,false,path),nexus::Invalid);
        nexus::Info info{};const auto stat=usb.Stat(usb_generation,path,info);
        if(R_SUCCEEDED(stat)) {
            R_UNLESS(!info.directory && info.size>=sizeof(nca.header),nexus::Invalid);
            R_TRY(nexusapp::ReadContent(physical_target,cs,nca.content_id,usb_generation,0,&nca.header,sizeof(nca.header)));
            R_TRY(nca::DecryptHeader(&nca.header,keys,nca.header));
            R_UNLESS(info.size==nca.header.size,nexus::Invalid);
            R_TRY(VerifyReusedNca(nca));R_TRY(HasRequiredTicket(nca.header,tickets));
            if(nca.header.distribution_type==nca::DistributionType_GameCard){
                // A correct original hash does not make cartridge-only data
                // suitable for installed use. Do not report a successful fix
                // while silently reusing the old broken copy.
                if(diagnostic)diagnostic->text="This USB copy still has cartridge-only content from an earlier install. Delete this game's USB copy in Installed games, then install it again from your original XCI. Saved progress is kept. The original XCI has not been changed.";
                nexus::Log("install-existing-cartridge-header",nexus::Invalid);
                return nexus::Invalid;
            }
            nca.skipped=true;return 0;
        }
        R_UNLESS(stat==MAKERESULT(140,205),stat); // only an explicit missing file permits creation
    }
    if (config.skip_if_already_installed || config.ticket_only) {
        R_TRY(ncmContentStorageHas(std::addressof(cs), std::addressof(nca.skipped), std::addressof(nca.content_id)));
        if (nca.skipped) {
            log_write("\tskipped nca as it's already installed ncmContentStorageHas()\n");
            R_TRY(ncmContentStorageReadContentIdFile(std::addressof(cs), std::addressof(nca.header), sizeof(nca.header), std::addressof(nca.content_id), 0));
            crypto::cryptoAes128Xts(std::addressof(nca.header), std::addressof(nca.header), keys.header_key, 0, 0x200, sizeof(nca.header), false);

            R_TRY(HasRequiredTicket(nca.header, tickets));
            R_SUCCEED();
        }
    }

    log_write("generateing placeholder\n");
    usb_verified.Forget(VerificationKey(nca,usb_generation).content);
    R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(cs), std::addressof(nca.placeholder_id)));
    log_write("creating placeholder\n");
    R_TRY(destination->Validate());
    R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(cs), std::addressof(nca.content_id), std::addressof(nca.placeholder_id), nca.size));
    nca.placeholder_owned = true;
    if(destination->IsUsb()) {
        char common[FS_MAX_PATH]{};
        R_TRY(ncmContentStorageGetPlaceHolderPath(&cs,common,sizeof(common),&nca.placeholder_id));
        R_UNLESS(nexus::ContentPath(common,true,nca.usb_path),nexus::Invalid);
    }

    log_write("opening thread\n");
    ThreadData t_data{this, tickets, std::addressof(nca), destination->IsUsb()};

    #define READ_THREAD_CORE 1
    #define DECOMPRESS_THREAD_CORE 2
    #define WRITE_THREAD_CORE 0
    // #define READ_THREAD_CORE 2
    // #define DECOMPRESS_THREAD_CORE 2
    // #define WRITE_THREAD_CORE 2

    Thread t_read{};
    R_TRY(utils::CreateThread(&t_read, readFunc, std::addressof(t_data), 1024*64));
    ON_SCOPE_EXIT(threadClose(&t_read));

    Thread t_decompress{};
    R_TRY(utils::CreateThread(&t_decompress, decompressFunc, std::addressof(t_data), 1024*64));
    ON_SCOPE_EXIT(threadClose(&t_decompress));

    Thread t_write{};
    R_TRY(utils::CreateThread(&t_write, writeFunc, std::addressof(t_data), 1024*64));
    ON_SCOPE_EXIT(threadClose(&t_write));

    log_write("starting threads\n");
    R_TRY(threadStart(std::addressof(t_read)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_read)));

    R_TRY(threadStart(std::addressof(t_decompress)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_decompress)));

    R_TRY(threadStart(std::addressof(t_write)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_write)));

    const auto waiter_progress = waiterForUEvent(t_data.GetProgressEvent());
    const auto waiter_cancel = waiterForUEvent(pbox->GetCancelEvent());
    const auto waiter_done = waiterForUEvent(t_data.GetDoneEvent());

    for (;;) {
        s32 idx;
        if (R_FAILED(waitMulti(&idx, UINT64_MAX, waiter_progress, waiter_cancel, waiter_done))) {
            break;
        }

        if (!idx) {
            pbox->UpdateTransfer(t_data.GetWriteOffset(), t_data.GetWriteSize());
            pbox->ContentBytes(t_data.GetWriteOffset(),t_data.GetWriteSize());
        } else {
            break;
        }
    }

    // wait for all threads to close.
    log_write("waiting for threads to close\n");
    while (t_data.IsAnyRunning()) {
        if(R_FAILED(t_data.GetResults()))source->SignalCancel();
        t_data.WakeAllThreads();
        pbox->Yield();

        if (R_FAILED(waitSingleHandle(t_read.handle, 1000))) {
            continue;
        } else if (R_FAILED(waitSingleHandle(t_decompress.handle, 1000))) {
            continue;
        } else if (R_FAILED(waitSingleHandle(t_write.handle, 1000))) {
            continue;
        }
        break;
    }
    log_write("threads closed\n");

    // if any of the threads failed, wake up all threads so they can exit.
    if (R_FAILED(t_data.GetResults())) {
        log_write("some reads failed, waking threads: %s\n", nca.name.c_str());
        log_write("returning due to fail: %s\n", nca.name.c_str());
        return t_data.GetResults();
    }
    R_TRY(t_data.GetResults());
    // A successful worker result alone must not promote a truncated write.
    R_UNLESS(t_data.GetWriteSize()>0 && t_data.GetWriteOffset()==t_data.GetWriteSize(),Result_YatiInvalidNcaSha256);

    pbox->ContentBytes(t_data.GetWriteOffset(),t_data.GetWriteSize());
    pbox->ContentPhase("Checking streamed hash");
    NcmContentId content_id{};
    std::memcpy(std::addressof(content_id), nca.hash, sizeof(content_id));

    log_write("old id: %s new id: %s\n", utils::hexIdToStr(nca.content_id).str, utils::hexIdToStr(content_id).str);
    if (!config.skip_nca_hash_verify || nca.verified_alias) {
        const void* expected=nca.has_expected_hash ? static_cast<const void*>(nca.expected_hash) : &nca.content_id;
        if(!t_data.source_hash.Matches(expected,nca.has_expected_hash?32:16,&nca.converted_hash,nullptr,nca.source_digest)) {
            if(diagnostic)diagnostic->Set(nexus::install::Failure::Hash,nca.name,diagnostic_title);
            return Result_YatiInvalidNcaSha256;
        }
        log_write("NCA source verified%s: %s\n",nca.converted_hash?" (distribution conversion)":"",nca.name.c_str());
        pbox->ContentPhase("Written / source hash OK");
        nca.usb_written.key=VerificationKey(nca,usb_generation);
        nca.usb_written.source_matched=true;
        nca.usb_written.complete=true;
    } else {
        log_write("skipping nca sha256 verify\n");
    }

    R_SUCCEED();
}

Result Yati::InstallNca(std::span<TikCollection> tickets, NcaCollection& nca) {
    if(diagnostic)diagnostic->Clear();
    nca.header_size_issue=nexus::install::HeaderSizeIssue::None;
    log_write("in install nca\n");
    pbox->NewTransfer(nca.name);
    if (!nca.has_expected_hash) R_UNLESS(nexus::compat::ContentName(nca.name,&nca.content_id), Result_YatiNcaNotFound);

    // Internal worker threads have joined before diagnostics are updated.
    const auto install_rc=InstallNcaInternal(tickets, nca);
    if(R_FAILED(install_rc)&&quick_usb_final_verify)usb_verified.Forget(VerificationKey(nca,usb_generation).content);
    if(R_FAILED(install_rc) && diagnostic && nca.header_size_issue!=nexus::install::HeaderSizeIssue::None) {
        const auto issue=nca.header_size_issue;
        diagnostic->Set(nexus::install::HeaderFailure(issue),
            nca.name,diagnostic_title,
            issue==nexus::install::HeaderSizeIssue::Metadata ? nca.expected_size : u64(nca.size),s64(nca.header.size));
    }
    if(R_FAILED(install_rc) && diagnostic && diagnostic->text.empty())
        diagnostic->Set(nexus::install::Failure::Content,nca.name,diagnostic_title);
    R_TRY(install_rc);

    fs::FsPath path;
    if (nca.skipped) {
        R_TRY(ncmContentStorageGetPath(std::addressof(cs), path, sizeof(path), std::addressof(nca.content_id)));
    } else {
        pbox->ContentPhase(destination->IsUsb()?"Flushing to USB":"Flushing to SD");
        R_TRY(ncmContentStorageFlushPlaceHolder(std::addressof(cs)));
        pbox->ContentPhase("Saved / awaiting registration");
        nca.usb_written.flushed=true;
        R_TRY(ncmContentStorageGetPlaceHolderPath(std::addressof(cs), path, sizeof(path), std::addressof(nca.placeholder_id)));
    }

    nexusapp::MetadataStage control_stage;
    if(destination->IsUsb() && nca.header.content_type == nca::ContentType_Control) {
        R_TRY(control_stage.Open(path.s,path));
    }
    if (nca.header.content_type == nca::ContentType_Program) {
        // todo: verify npdm key.
    } else if (nca.header.content_type == nca::ContentType_Control) {
        NacpLanguageEntry entry;
        std::vector<u8> icon;
        // this may fail if tickets aren't installed and the nca uses title key crypto.
        if (R_SUCCEEDED(nca::ParseControl(path, nca.header.program_id, &entry, sizeof(entry), &icon))) {
            pbox->SetTitle(entry.name).SetImageData(icon);
        }
    }

    if(destination->IsUsb() && !source->IsStream() && nca.type!=NcmContentType_Meta && !nca.skipped) {
        // Commit verified individual files so the next attempt need not copy
        // the whole package again. Do not publish NS/metadata until all pass.
        R_TRY(PublishNca(nca));Cleanup(nca);nca.placeholder_owned=false;nca.skipped=true;
        if(receipt)++receipt->verified;
        R_TRY(Checkpoint(nexusapp::InstallStep::ContentVerified));
    }
    R_SUCCEED();
}

Result Yati::ReadSourceExact(void* data, s64 offset, s64 size, Result invalid_read) {
    return source::ReadExact(*source, data, offset, size, invalid_read, [this]() -> Result {
        R_TRY(pbox->ShouldExitResult());
        return destination->Validate();
    });
}

Result Yati::FindContent(const NcmPackagedContentInfo& packed, u64 owner,
    const container::Collections& collections, const container::CollectionEntry*& found) {
    found=nullptr;
    const auto& info=packed.info;
    for (const auto& entry:collections) {
        NcmContentId id{};
        if (nexus::compat::ContentName(entry.name,&id) && !memcmp(&id,&info.content_id,sizeof(id))) {
            R_UNLESS(!found,Result_YatiNcaNotFound); // ambiguous duplicate, never pick first
            found=&entry;
        }
    }
    if (found) R_SUCCEED();
    // Seeking backwards is impossible on forward-only streams. The bounded
    // fallback is intentionally for small, uncompressed NCA entries only.
    u64 expected_size{};ncmContentInfoSizeToU64(&info,&expected_size);
    R_UNLESS(!source->IsStream() && expected_size>=sizeof(nca::Header) && expected_size<=16*1024*1024,
        Result_YatiNcaNotFound);
    int expected_type=-1;
    switch (info.content_type) {
        case NcmContentType_Program: expected_type=nca::ContentType_Program;break;
        case NcmContentType_Control: expected_type=nca::ContentType_Control;break;
        case NcmContentType_HtmlDocument: expected_type=nca::ContentType_Manual;break;
        case NcmContentType_LegalInformation: expected_type=nca::ContentType_Manual;break;
        case NcmContentType_Data: expected_type=nca::ContentType_Data;break;
        default: return Result_YatiNcaNotFound;
    }
    std::vector<u8> buffer(1024*1024);
    for (const auto& entry:collections) {
        NcmContentId ignored{};
        if (entry.size!=s64(expected_size) || !nexus::compat::ContentName(entry.name,&ignored,false)) continue;
        R_UNLESS(entry.offset>=0 && entry.offset<=INT64_MAX-entry.size,Result_YatiNcaNotFound);
        R_UNLESS(alias_candidates_left && alias_bytes_left>=expected_size,Result_YatiNcaNotFound);
        --alias_candidates_left;alias_bytes_left-=expected_size;
        pbox->NewTransfer("Verify missing content: "+entry.name);
        Sha256Context hash;sha256ContextCreate(&hash);
        bool candidate=true;
        for (u64 off=0;off<expected_size;) {
            const auto count=std::min<u64>(buffer.size(),expected_size-off);
            R_TRY(ReadSourceExact(buffer.data(),entry.offset+off,count,Result_YatiInvalidNcaSha256));
            if (!off) {
                nca::Header header{};
                bool decoded{};
                if (!memcmp(buffer.data()+0x200,"NCA3",4)) {
                    memcpy(&header,buffer.data(),sizeof(header));
                    decoded=header.content_type==nca::ContentType_Control && header.distribution_type==nca::DistributionType_System;
                } else decoded=R_SUCCEEDED(nca::DecryptHeader(buffer.data(),keys,header));
                candidate=decoded &&
                    header.size==expected_size && header.program_id==owner+info.id_offset &&
                    (header.content_type==expected_type || (expected_type==nca::ContentType_Data && header.content_type==nca::ContentType_PublicData));
                if (!candidate) break;
            }
            sha256ContextUpdate(&hash,buffer.data(),count);off+=count;pbox->AddTransferBytes(count);pbox->UpdateTransfer(off,expected_size);
        }
        if (!candidate) continue;
        u8 digest[32];sha256ContextGetHash(&hash,digest);
        if (memcmp(digest,packed.hash,sizeof(digest))) continue;
        R_UNLESS(!found,Result_YatiNcaNotFound);
        found=&entry;
    }
    R_UNLESS(found,Result_YatiNcaNotFound);
    log_write("Verified NCA filename alias: %s -> %s\n",found->name.c_str(),utils::hexIdToStr(info.content_id).str);
    R_SUCCEED();
}

Result Yati::InstallCnmtNca(std::span<TikCollection> tickets, CnmtCollection& cnmt, const container::Collections& collections) {
    diagnostic_title=0; // A previous CNMT must never be blamed for this one's failure.
    R_TRY(InstallNca(tickets, cnmt));

    fs::FsPath path;
    if (cnmt.skipped) {
        R_TRY(ncmContentStorageGetPath(std::addressof(cs), path, sizeof(path), std::addressof(cnmt.content_id)));
    } else {
        R_TRY(ncmContentStorageFlushPlaceHolder(std::addressof(cs)));
        R_TRY(ncmContentStorageGetPlaceHolderPath(std::addressof(cs), path, sizeof(path), std::addressof(cnmt.placeholder_id)));
    }

    ncm::PackagedContentMeta header;
    std::vector<NcmPackagedContentInfo> infos;
    nexusapp::MetadataStage stage;
    if(destination->IsUsb()) R_TRY(stage.Open(path.s,path));
    R_TRY(nca::ParseCnmt(path, cnmt.header.program_id, header, cnmt.extended_header, infos, &cnmt.metadata_tail));
    cnmt.source_install_type=header.install_type;
    cnmt.key.id=header.title_id;cnmt.key.type=header.meta_type;
    const auto owner=header.meta_type==NcmContentMetaType_Patch ? ApplicationId(cnmt) : header.title_id;
    diagnostic_title=header.title_id;

    for (const auto& packed_info : infos) {
        const auto& info = packed_info.info;
        if (info.content_type == NcmContentType_DeltaFragment) {
            continue;
        }

        const auto str = utils::hexIdToStr(info.content_id);
        const container::CollectionEntry* entry{};
        const auto find_rc=FindContent(packed_info,owner,collections,entry);
        if(R_FAILED(find_rc) && diagnostic)
            diagnostic->Set(nexus::install::Failure::MissingContent,std::string(str.str)+".nca",header.title_id);
        R_TRY(find_rc);
        u64 expected_size{};ncmContentInfoSizeToU64(&info,&expected_size);
        if(nexus::install::RawSizeMismatch(entry->name,entry->size,expected_size)) {
            if(diagnostic)diagnostic->Set(nexus::install::Failure::Size,entry->name,header.title_id,expected_size,entry->size);
            log_write("C6: rejected CNMT/NCA size mismatch before content copy: %s\n",entry->name.c_str());
            return Result_YatiInvalidNcaSha256;
        }

        log_write("found: %s\n", str.str);
        cnmt.infos.emplace_back(packed_info);
        auto& nca = cnmt.ncas.emplace_back(*entry);
        nca.type = info.content_type;
        nca.content_id=info.content_id;
        memcpy(nca.expected_hash,packed_info.hash,sizeof(nca.expected_hash));
        ncmContentInfoSizeToU64(&info,&nca.expected_size);
        nca.has_expected_hash=true;
        NcmContentId named{};
        nca.verified_alias=!nexus::compat::ContentName(entry->name,&named) || memcmp(&named,&info.content_id,sizeof(named));
    }

    // update header
    cnmt.meta_header = header.meta_header;
    cnmt.meta_header.content_count = cnmt.infos.size() + 1;
    cnmt.meta_header.storage_id = 0;

    cnmt.key.id = header.title_id;
    cnmt.key.version = header.title_version;
    cnmt.key.type = header.meta_type;
    cnmt.key.install_type = NcmContentInstallType_Full;
    R_UNLESS(ApplicationId(cnmt),nexus::Invalid); // user applications only, never firmware metadata
    std::memset(cnmt.key.padding, 0, sizeof(cnmt.key.padding));

    cnmt.content_info.content_id = cnmt.content_id;
    cnmt.content_info.content_type = NcmContentType_Meta;
    cnmt.content_info.attr = 0;
    ncmU64ToContentInfoSize(cnmt.header.size, &cnmt.content_info);
    cnmt.content_info.id_offset = 0;

    if (config.lower_system_version) {
        auto extended_header = (ncm::ExtendedHeader*)cnmt.extended_header.data();
        log_write("patching version\n");
        if (cnmt.key.type == NcmContentMetaType_Application) {
            extended_header->application.required_system_version = 0;
        } else if (cnmt.key.type == NcmContentMetaType_Patch) {
            extended_header->patch.required_system_version = 0;
        }
    }

    // sort ncas
    const auto sorter = [](NcaCollection& lhs, NcaCollection& rhs) -> bool {
        return lhs.type > rhs.type;
    };

    std::ranges::sort(cnmt.ncas, sorter);

    log_write("found all cnmts\n");
    R_SUCCEED();
}

Result Yati::ParseTicketsIntoCollection(std::vector<TikCollection>& tickets, const container::Collections& collections, bool read_data) {
    for (const auto& collection : collections) {
        if (collection.name.ends_with(".tik")) {
            const auto cert = nexus::archive::Certificate(collections,collection.name);
            R_UNLESS(cert, Result_YatiCertNotFound);
            TikCollection entry{};
            keys::parse_hex_key(entry.rights_id.c, collection.name.c_str());
            entry.ticket.resize(collection.size);
            entry.cert.resize(cert->size);

            // only supported on non-stream installs.
            if (read_data) {
                R_TRY(ReadSourceExact(entry.ticket.data(), collection.offset, entry.ticket.size()));
                R_TRY(ReadSourceExact(entry.cert.data(), cert->offset, entry.cert.size()));
            }

            tickets.emplace_back(entry);
        }
    }

    R_SUCCEED();
}

Result Yati::GetLatestVersion(const CnmtCollection& cnmt, u32& version_out, bool& skip) {
    const auto app_id = ApplicationId(cnmt);
    R_UNLESS(app_id,nexus::Invalid);
    version_out = cnmt.key.version;

    for (auto& db : ncm_db) {
        s32 db_list_total;
        s32 db_list_count;
        std::vector<NcmContentMetaKey> keys(1);
        if (R_SUCCEEDED(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), NcmContentMetaType_Unknown, app_id, 0, UINT64_MAX, NcmContentInstallType_Full))) {
            if (db_list_total != keys.size()) {
                keys.resize(db_list_total);
                if (keys.size()) {
                    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), NcmContentMetaType_Unknown, app_id, 0, UINT64_MAX, NcmContentInstallType_Full));
                }
            }

            for (auto& key : keys) {
                log_write("found record: %016lX type: %u version: %u\n", key.id, key.type, key.version);

                if (key.id == cnmt.key.id && cnmt.key.version == key.version && config.skip_if_already_installed) {
                    log_write("skipping as already installed\n");
                    skip = true;
                }

                // check if we are downgrading
                if (cnmt.key.type == NcmContentMetaType_Patch) {
                    if (cnmt.key.type == key.type && cnmt.key.version < key.version && !config.allow_downgrade) {
                        log_write("skipping due to it being lower\n");
                        skip = true;
                    }
                } else {
                    version_out = std::max(version_out, key.version);
                }
            }
        }
    }

    R_SUCCEED();
}

Result Yati::ShouldSkip(const CnmtCollection& cnmt, bool& skip) {
    if (!skip && config.skip_if_already_installed) {
        bool has;
        R_TRY(ncmContentMetaDatabaseHas(std::addressof(db), std::addressof(has), std::addressof(cnmt.key)));
        if (has) {
            log_write("\tskipping: [ncmContentMetaDatabaseHas()]\n");
            skip = true;
        }
    }

    // skip invalid types
    if (!skip) {
        if (!(cnmt.key.type & 0x80)) {
            log_write("\tskipping: invalid: %u\n", cnmt.key.type);
            skip = true;
        } else if (config.skip_base && cnmt.key.type == NcmContentMetaType_Application) {
            log_write("\tskipping: [NcmContentMetaType_Application]\n");
            skip = true;
        } else if (config.skip_patch && cnmt.key.type == NcmContentMetaType_Patch) {
            log_write("\tskipping: [NcmContentMetaType_Application]\n");
            skip = true;
        } else if (config.skip_addon && cnmt.key.type == NcmContentMetaType_AddOnContent) {
            log_write("\tskipping: [NcmContentMetaType_AddOnContent]\n");
            skip = true;
        } else if (config.skip_data_patch && cnmt.key.type == NcmContentMetaType_DataPatch) {
            log_write("\tskipping: [NcmContentMetaType_DataPatch]\n");
            skip = true;
        }
    }

    R_SUCCEED();
}

Result Yati::ImportTickets(std::span<TikCollection> tickets) {
    for (auto& ticket : tickets) {
        if (ticket.required || config.ticket_only) {
            if (config.skip_ticket) {
                log_write("WARNING: skipping ticket install, but it's required!\n");
            } else {
                nexusapp::UsbTicketPair original;
                const bool archive=destination->IsUsb() && archive_usb_tickets && !ticket.patched;
                if(archive){original.ticket=ticket.ticket;original.certificate=ticket.cert;}
                if (!ticket.patched) {
                    log_write("patching ticket\n");
                    R_TRY(es::PatchTicket(ticket.ticket, ticket.cert, ticket.key_gen, keys, config.convert_to_common_ticket));
                    ticket.patched = true;
                }

                log_write("installing ticket\n");
                pbox->TransferEvent("Installing ticket and certificate");
                R_TRY(es::ImportTicket(ticket.ticket.data(), ticket.ticket.size(), ticket.cert.data(), ticket.cert.size()));
                if(archive){
                    // Original bytes were retained before PatchTicket, but
                    // only an accepted install may create a durable archive.
                    const auto archived=nexusapp::SaveUsbTicketPair(usb_generation,ticket.rights_id,original.ticket,original.certificate);
                    if(R_FAILED(archived)){
                        nexus::Log("usb-ticket-archive-failed",archived);
                        pbox->SetProgressNote("Ticket copy could not be saved. Keep your original game backup.");
                        if(diagnostic){
                            if(!diagnostic->completion.empty())diagnostic->completion+='\n';
                            diagnostic->completion+="The USB ticket copy could not be saved. Keep the original game backup; importing this game in another SD/emuMMC may require reinstalling it.";
                        }
                    }
                }
                ticket.required = false;
            }
        }
    }

    R_SUCCEED();
}

Result Yati::RemoveInstalledNcas(const CnmtCollection&) {
    // Retain previous content on failed installs. Source removal is exclusively
    // the separately confirmed, verified move operation in USB Loader.
    return destination->Validate();
}

Result Yati::PublishNca(const NcaCollection& nca) {
    const Result result=[&]() -> Result {
    R_TRY(destination->Validate());
    // Ticket-only mode can reuse content even though skip-if-installed is off.
    // Reuse is still a USB publication: prove physical bytes before ensuring it.
    if(nca.skipped)return destination->IsUsb()?VerifyReusedNca(nca):0;
    R_UNLESS(nca.placeholder_owned,nexus::Invalid);
    pbox->ContentFile(nca.name,"NCA","Registering NCA",nca.header.size);
    pbox->ContentPhase("Registering NCA");
    const auto rc=ncmContentStorageRegister(&cs,&nca.content_id,&nca.placeholder_id);
    if(R_FAILED(rc)) {
        nexus::Log("P2-register-nca",rc);
        // Only NCM ContentAlreadyExists permits duplicate verification. A
        // matching hash cannot turn an I/O/map/generation/IPC failure into success.
        R_UNLESS(rc==nexus::registration::ContentAlreadyExists,rc);
        // Existing ID: reuse only after exact byte verification. Never remove
        // native SD content merely because routed Has() reports it.
        bool has{};R_TRY(ncmContentStorageHas(&cs,&has,&nca.content_id));R_UNLESS(has,rc);
        s64 size{};R_TRY(ncmContentStorageGetSizeFromContentId(&cs,&size,&nca.content_id));
        R_UNLESS(size==static_cast<s64>(nca.header.size),rc);
        // A publication collision is not proof that our writes won. In quick
        // mode perform one full physical pass, checking both the exact output
        // digest and the original CNMT representation in that same pass.
        if(quick_usb_final_verify&&destination->IsUsb())
            return VerifyReusedNca(nca,false,nca.hash,rc);
        Sha256Context hash;sha256ContextCreate(&hash);
        std::vector<u8> buffer(1024*1024);
        for(s64 off=0;off<size;) {
            R_TRY(pbox->ShouldExitResult());
            const auto count=std::min<s64>(buffer.size(),size-off);
            R_TRY(nexusapp::ReadContent(physical_target,cs,nca.content_id,usb_generation,off,buffer.data(),count));
            sha256ContextUpdate(&hash,buffer.data(),count);off+=count;
        }
        u8 actual[32];sha256ContextGetHash(&hash,actual);
        R_UNLESS(!memcmp(actual,nca.hash,sizeof(actual)),rc);
    }
    // Only successful registration of our own placeholder can use the streamed
    // write evidence. The ordinary/default path still reads the entire NCA.
    return destination->IsUsb()?VerifyReusedNca(nca,true):destination->Verify(cs,nca.content_id,nca.header.size);
    }();
    if(R_FAILED(result)&&quick_usb_final_verify)usb_verified.Forget(VerificationKey(nca,usb_generation).content);
    if(R_FAILED(result) && diagnostic && diagnostic->text.empty())
        diagnostic->Set(nexus::install::Failure::Publication,nca.name,diagnostic_title);
    return result;
}

Result Yati::VerifyReusedNca(const NcaCollection& nca,bool allow_written,const u8* exact_output_hash,Result exact_hash_error) {
    const u64 size=nca.has_expected_hash?nca.expected_size:nca.header.size;
    const auto key=VerificationKey(nca,usb_generation);
    bool verified{};
    ON_SCOPE_EXIT(if(!verified&&quick_usb_final_verify)usb_verified.Forget(key.content));
    R_UNLESS(size && size<=INT64_MAX && size==nca.header.size,nexus::Invalid);
    const auto cached=quick_usb_final_verify&&!exact_output_hash?usb_verified.Find(key):nullptr;
    const bool written=quick_usb_final_verify&&!exact_output_hash&&allow_written&&nca.usb_written.Matches(key);
    const bool quick=cached||written;
    pbox->ContentFile(nca.name,"NCA",quick?"Quick USB check":"Verifying stored NCA",size);
    if(quick)pbox->ContentPhase("Quick check: header + size + write proof");
    const auto progress=usb_verification;
    if(progress) {
        R_UNLESS(progress->completed_bytes<=progress->total_bytes &&
            size<=progress->total_bytes-progress->completed_bytes &&
            progress->completed_files<progress->total_files,nexus::Invalid);
        pbox->SetProgressNote(std::string(quick?"Quick check, file ":"Checking saved bytes, file ")+std::to_string(progress->completed_files+1)+
            " of "+std::to_string(progress->total_files));
    } else {
        pbox->NewTransfer(quick?"Quick USB check":"Checking USB file");
    }
    const u64 total=progress?progress->total_bytes:size;
    const u64 completed=progress?progress->completed_bytes:0;
    nexus::verification::Digest header_hash{};
    bool header_known{};
    if(quick) {
        // This opt-in does not claim to reread body bytes. Source hashing and
        // acknowledged writes (or an earlier full physical hash) are already
        // proven for this exact content expectation and media generation.
        R_TRY(pbox->ShouldExitResult());R_TRY(destination->Validate());
        std::array<u8,sizeof(nca::Header)> header{};
        R_TRY(nexusapp::ReadContent(physical_target,cs,nca.content_id,usb_generation,0,header.data(),header.size()));
        sha256CalculateHash(header_hash.data(),header.data(),header.size());header_known=true;
        R_UNLESS(header_hash==(cached?*cached:nca.usb_written.header),Result_YatiInvalidNcaSha256);
        nexus::Log(written?"usb-verify-quick-written":"usb-verify-quick-reused",0,size);
    } else {
        if(quick_usb_final_verify)nexus::Log("usb-verify-full-reused",0,size);
        nca::ContentHash hash;
        if(nca.verified_alias)hash.EnablePlaintextOrigin();
        Sha256Context output_hash{};if(exact_output_hash)sha256ContextCreate(&output_hash);
        std::vector<u8> buffer(1024*1024);
        for(u64 off=0;off<size;) {
            R_TRY(pbox->ShouldExitResult());R_TRY(destination->Validate());
            const auto count=std::min<u64>(buffer.size(),size-off);
            R_TRY(nexusapp::ReadContent(physical_target,cs,nca.content_id,usb_generation,off,buffer.data(),count));
            if(quick_usb_final_verify&&!off&&count>=sizeof(nca::Header)){
                sha256CalculateHash(header_hash.data(),buffer.data(),sizeof(nca::Header));header_known=true;
            }
            hash.Update(buffer.data(),count,keys.header_key);
            if(exact_output_hash)sha256ContextUpdate(&output_hash,buffer.data(),count);
            off+=count;pbox->AddTransferBytes(count);pbox->ContentBytes(off,size);
            // Keep completion pending until hash, size and registration pass.
            pbox->UpdateTransfer(std::min(completed+off,total-std::max<u64>(1,total/1000)),total);
        }
        if(exact_output_hash){u8 digest[32];sha256ContextGetHash(&output_hash,digest);R_UNLESS(!memcmp(digest,exact_output_hash,32),exact_hash_error);}
        const void* expected=nca.has_expected_hash?static_cast<const void*>(nca.expected_hash):&nca.content_id;
        R_UNLESS(hash.Matches(expected,nca.has_expected_hash?32:16),Result_YatiInvalidNcaSha256);
    }
    R_TRY(pbox->ShouldExitResult());
    // Verify checks the physical size and invokes the P2 registration API.
    R_TRY(destination->Verify(cs,nca.content_id,size));
    R_TRY(pbox->ShouldExitResult());
    if(quick_usb_final_verify&&header_known)usb_verified.Remember(key,header_hash);
    verified=true;pbox->ContentPhase("Verified / registered");
    if(progress) {
        progress->completed_bytes+=size;
        ++progress->completed_files;
        pbox->SetProgressNote("Verified "+std::to_string(progress->completed_files)+
            " of "+std::to_string(progress->total_files)+" files");
    }
    const bool finished=!progress || progress->completed_files==progress->total_files;
    pbox->UpdateTransfer(finished?total:
        std::min(completed+size,total-std::max<u64>(1,total/1000)),total);
    return 0;
}

Result Yati::PublishNcas(const CnmtCollection& cnmt) {
    R_UNLESS(!usb_verification,nexus::Invalid);
    UsbVerificationProgress progress;
    const bool aggregate=destination->IsUsb();
    if(aggregate) {
        // CNMT sizes describe the uncompressed content. Container entry sizes
        // cannot be used here because NSZ/XCZ entries may be compressed.
        const auto add=[&](const NcaCollection& nca)->Result {
            const u64 size=nca.has_expected_hash?nca.expected_size:nca.header.size;
            R_UNLESS(size && size==nca.header.size &&
                size<=u64(INT64_MAX)-progress.total_bytes,nexus::Invalid);
            progress.total_bytes+=size;
            ++progress.total_files;
            return 0;
        };
        R_TRY(add(cnmt));
        for(const auto& nca:cnmt.ncas) {
            if(nca.type!=NcmContentType_DeltaFragment)R_TRY(add(nca));
        }
        usb_verification=&progress;
        pbox->NewTransfer(quick_usb_final_verify?"Final USB checks":"Verifying USB content");
        pbox->UpdateTransfer(0,progress.total_bytes);
    }
    ON_SCOPE_EXIT(
        usb_verification=nullptr;
        if(aggregate)pbox->SetProgressNote("");
    );

    // Keep one byte-weighted bar throughout this title's final publication
    // pass, including both reused and newly copied content and the CNMT.
    log_write("registering or verifying reused cnmt nca\n");
    R_TRY(PublishNca(cnmt));
    log_write("registered cnmt nca\n");

    for (auto& nca : cnmt.ncas) {
        if (nca.type != NcmContentType_DeltaFragment) {
            log_write("registering nca: %s\n", nca.name.c_str());
            R_TRY(PublishNca(nca));
            log_write("registered nca: %s\n", nca.name.c_str());
        }
    }

    log_write("register'd all ncas\n");
    return 0;
}

Result Yati::RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num) {
    const auto app_id = ApplicationId(cnmt);
    R_UNLESS(app_id,nexus::Invalid);
    // Streaming installation may last have parsed a different NCA/CNMT.
    diagnostic_title=cnmt.key.id;

    R_TRY(PublishNcas(cnmt));

    // build ncm meta and push to the database.
    BufHelper buf{};
    buf.write(std::addressof(cnmt.meta_header), sizeof(cnmt.meta_header));
    buf.write(cnmt.extended_header.data(), cnmt.extended_header.size());
    buf.write(std::addressof(cnmt.content_info), sizeof(cnmt.content_info));

    for (auto& info : cnmt.infos) {
        buf.write(std::addressof(info.info), sizeof(info.info));
    }

    buf.write(cnmt.metadata_tail.data(),cnmt.metadata_tail.size());
    R_TRY(destination->Validate());
    pbox->NewTransfer("Updating ncm database"_i18n);
    R_TRY(ncmContentMetaDatabaseSet(std::addressof(db), std::addressof(cnmt.key), buf.buf.data(), buf.tell()));
    R_TRY(ncmContentMetaDatabaseCommit(std::addressof(db)));
    u64 stored_size{},read_size{};
    R_TRY(ncmContentMetaDatabaseGetSize(&db,&stored_size,&cnmt.key));
    R_UNLESS(stored_size==buf.tell(),nexus::Invalid);
    std::vector<u8> stored(stored_size);
    R_TRY(ncmContentMetaDatabaseGet(&db,&cnmt.key,&read_size,stored.data(),stored.size()));
    R_UNLESS(read_size==stored.size() && !memcmp(stored.data(),buf.buf.data(),stored.size()),nexus::Invalid);
    R_TRY(Checkpoint(nexusapp::InstallStep::MetadataCommitted));
    pbox->TransferEvent("Content metadata registered");

    // push record.
    ncm::ContentStorageRecord content_storage_record{};
    content_storage_record.key = cnmt.key;
    content_storage_record.storage_id = storage_id;
    pbox->NewTransfer("Pushing application record"_i18n);

    std::vector<ncm::ContentStorageRecord> records;
    R_TRY(ns::GetApplicationRecords(app_id,records));
    std::erase_if(records,[&](const auto& r){return r.key.id==cnmt.key.id && r.key.type==cnmt.key.type;});
    records.push_back(content_storage_record);
    R_TRY(destination->Validate());
    R_TRY(ns::PushApplicationRecord(app_id,records.data(),records.size()));
    std::vector<ncm::ContentStorageRecord> read_records;
    R_TRY(ns::GetApplicationRecords(app_id,read_records));
    R_UNLESS(std::any_of(read_records.begin(),read_records.end(),[&](const auto& r){
        // Reserved wire padding is not part of a record's identity.
        return r.storage_id==storage_id && r.key.id==cnmt.key.id && r.key.version==cnmt.key.version &&
            r.key.type==cnmt.key.type && r.key.install_type==cnmt.key.install_type;
    }),nexus::Invalid);
    R_TRY(Checkpoint(nexusapp::InstallStep::RecordPublished));
    pbox->TransferEvent("Application record added to HOME");
    // USB import's transaction owner commits the complete application family
    // before applying non-rollbackable launch-version/HOME-cache changes.
    if(defer_postcommit)return 0;
    if (hosversionAtLeast(6,0,0)) {
        R_TRY(avmInitialize());
        ON_SCOPE_EXIT(avmExit());

        R_TRY(avmPushLaunchVersion(app_id, latest_version_num));
    }
    log_write("pushed\n");
    if(destination->IsUsb() && (cnmt.key.type==NcmContentMetaType_Application || cnmt.key.type==NcmContentMetaType_Patch) &&
       std::any_of(cnmt.infos.begin(),cnmt.infos.end(),[](const auto& i){return i.info.content_type==NcmContentType_Program;})){
        nexusapp::Game badge;badge.id=app_id;badge.badge_state=1;
        const auto queued=nexusapp::QueueHomeRefresh(app_id);
        const auto rc=R_SUCCEEDED(queued)?nexusapp::UpdateHomeBadge(badge):queued;
        if(R_FAILED(rc))nexus::Log("install-home-badge",rc,app_id);
        const auto finished=R_SUCCEEDED(rc)?nexusapp::FinishHomeRefresh(app_id):rc;
        if(R_FAILED(finished) && diagnostic){
            if(!diagnostic->completion.empty())diagnostic->completion+='\n';
            diagnostic->completion+=R_FAILED(queued)?"Installed and verified; icon refresh pending (retry record could not be saved). Open Installed to retry.":
                             "Installed and verified; icon refresh pending. Saved for retry when Installed is opened.";
        }
    }

    R_SUCCEED();
}

Result InstallInternal(ui::ProgressBox* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override) {
    auto yati = std::make_unique<Yati>(pbox, source);
    R_TRY(yati->Setup(override));

    std::vector<TikCollection> tickets{};
    R_TRY(yati->ParseTicketsIntoCollection(tickets, collections, true));

    std::vector<CnmtCollection> cnmts{};
    for (const auto& collection : collections) {
        log_write("found collection: %s\n", collection.name.c_str());
        if (collection.name.ends_with(".cnmt.nca") || collection.name.ends_with(".cnmt.ncz")) {
            auto& cnmt = cnmts.emplace_back(NcaCollection{collection});
            cnmt.type = NcmContentType_Meta;
        }
    }

    size_t title_index{};
    for (auto& cnmt : cnmts) {
        ui::ProgressScope title_scope(pbox,nexus::WorkPart(title_index,cnmts.size()),nexus::WorkPart(title_index+1,cnmts.size()));++title_index;
        ON_SCOPE_EXIT(
            yati->Cleanup(cnmt);
            for (auto& nca : cnmt.ncas) {
                yati->Cleanup(nca);
            }
        );

        {ui::ProgressScope meta_scope(pbox,0,200);R_TRY(yati->InstallCnmtNca(tickets, cnmt, collections));}

        u32 latest_version_num;
        bool skip = false;
        R_TRY(yati->GetLatestVersion(cnmt, latest_version_num, skip));
        R_TRY(yati->ShouldSkip(cnmt, skip));

        if (skip) {
            log_write("skipping install!\n");
            if(yati->outcome)++yati->outcome->skipped_records;
            continue;
        }

        if(!source->IsStream())std::stable_sort(cnmt.ncas.begin(),cnmt.ncas.end(),[](const auto& a,const auto& b){
            return (a.type==NcmContentType_Control)>(b.type==NcmContentType_Control);
        });
        log_write("installing nca's\n");
        u64 total{},done{};for(const auto& nca:cnmt.ncas)total+=std::max<s64>(nca.size,1);
        for (auto& nca : cnmt.ncas) {
            const auto weight=std::max<s64>(nca.size,1);
            ui::ProgressScope content_scope(pbox,200+nexus::WorkPart(done,total,8800),200+nexus::WorkPart(done+weight,total,8800));
            R_TRY(yati->InstallNca(tickets, nca));done+=weight;
        }
        ui::ProgressScope finish_scope(pbox,9000,10000);

        R_TRY(yati->ImportTickets(tickets));
        R_TRY(yati->RemoveInstalledNcas(cnmt));
        R_TRY(yati->RegisterNcasAndPushRecord(cnmt, latest_version_num));
        if(yati->outcome)++yati->outcome->installed_records;
    }

    log_write("success!\n");
    R_SUCCEED();
}

Result InstallInternalStream(ui::ProgressBox* pbox, source::Base* source, container::Collections collections, const ConfigOverride& override) {
    auto yati = std::make_unique<Yati>(pbox, source);
    R_TRY(yati->Setup(override));

    // not supported with stream installs (yet).
    yati->config.skip_if_already_installed = false;
    yati->config.convert_to_standard_crypto = false;
    yati->config.lower_master_key = false;
    // Full CNMT validation below requires the streamed source digest, even if
    // another caller has a legacy skip-hash setting enabled.
    yati->config.skip_nca_hash_verify = false;

    std::vector<NcaCollection> ncas{};
    std::vector<CnmtCollection> cnmts{};
    std::vector<TikCollection> tickets{};

    ON_SCOPE_EXIT(
        for (const auto& cnmt : cnmts) {
            yati->Cleanup(cnmt);
        }

        for (const auto& nca : ncas) {
            yati->Cleanup(nca);
        }
    );

    // fill ticket entries, the data will be filled later on.
    R_TRY(yati->ParseTicketsIntoCollection(tickets, collections, false));

    // sort based on lowest offset.
    const auto sorter = [](const container::CollectionEntry& lhs, const container::CollectionEntry& rhs) -> bool {
        return lhs.offset < rhs.offset;
    };

    std::ranges::sort(collections, sorter);

    u64 package_work{},package_done{};for(const auto& entry:collections)package_work+=std::max<s64>(entry.size,1);
    for (const auto& collection : collections) {
        const auto weight=std::max<s64>(collection.size,1);
        ui::ProgressScope content_scope(pbox,nexus::WorkPart(package_done,package_work,9000),nexus::WorkPart(package_done+weight,package_work,9000));
        package_done+=weight;
        if (collection.name.ends_with(".nca") || collection.name.ends_with(".ncz")) {
            auto& nca = ncas.emplace_back(NcaCollection{collection});
            if (collection.name.ends_with(".cnmt.nca") || collection.name.ends_with(".cnmt.ncz")) {
                auto& cnmt = cnmts.emplace_back(nca);
                cnmt.type = NcmContentType_Meta;
                R_TRY(yati->InstallCnmtNca(tickets, cnmt, collections));
            } else {
                yati->diagnostic_title=0; // Forward-only content may precede its owner metadata.
                R_TRY(yati->InstallNca(tickets, nca));
            }
        } else if (collection.name.ends_with(".tik") || collection.name.ends_with(".cert")) {
            R_UNLESS(!nexus::archive::Canonical(collection.name).empty(), Result_YatiCertNotFound);
            FsRightsId rights_id{};
            keys::parse_hex_key(rights_id.c, collection.name.c_str());

            auto entry = std::ranges::find_if(tickets, [&rights_id](auto& e){
                return !std::memcmp(&rights_id, &e.rights_id, sizeof(rights_id));
            });

            // this will never fail...but just in case.
            R_UNLESS(entry != tickets.end(), Result_YatiCertNotFound);

            if (collection.name.ends_with(".tik")) {
                R_TRY(yati->ReadSourceExact(entry->ticket.data(), collection.offset, entry->ticket.size()));
            } else {
                R_TRY(yati->ReadSourceExact(entry->cert.data(), collection.offset, entry->cert.size()));
            }
        }
    }

    // Do not publish titles from an aborted/truncated upload, including a
    // missing trailing ticket/certificate or unrelated declared container file.
    s64 end{};
    for(const auto& entry:collections){
        R_UNLESS(entry.offset>=0&&entry.size>=0&&entry.offset<=INT64_MAX-entry.size,Result_NspInvalidHeader);
        end=std::max(end,entry.offset+entry.size);
    }
    R_TRY(source->FinishRead(end));
    pbox->TransferEvent("Upload complete. Checking content metadata before registration.");

    // Bind every streamed digest to its full CNMT hash/expanded size before
    // publishing ANY title. The metadata may be last in the incoming package.
    for (auto& cnmt : cnmts) {
        // copy nca structs into cnmt.
        for (auto& cnmt_nca : cnmt.ncas) {
            auto it = std::ranges::find_if(ncas, [&cnmt_nca](auto& e){
                return e.name == cnmt_nca.name;
            });

            R_UNLESS(it != ncas.cend(), Result_YatiNczSectionNotFound);
            R_UNLESS(cnmt_nca.has_expected_hash && it->header.size==cnmt_nca.expected_size &&
                !memcmp(it->source_digest,cnmt_nca.expected_hash,32),Result_YatiInvalidNcaSha256);
            const auto type=cnmt_nca.type;
            const auto expected_size=cnmt_nca.expected_size;
            std::array<u8,32> expected{};memcpy(expected.data(),cnmt_nca.expected_hash,32);
            cnmt_nca=*it;cnmt_nca.type=type;cnmt_nca.expected_size=expected_size;
            memcpy(cnmt_nca.expected_hash,expected.data(),32);cnmt_nca.has_expected_hash=true;
            cnmt_nca.usb_written.key=VerificationKey(cnmt_nca,yati->usb_generation);
        }
    }
    pbox->TransferEvent("CNMT hashes and content sizes checked.");
    size_t finished_titles{};
    for (auto& cnmt : cnmts) {
        const auto title_index=finished_titles++;
        ui::ProgressScope finish_scope(pbox,9000+nexus::WorkPart(title_index,cnmts.size(),1000),9000+nexus::WorkPart(finished_titles,cnmts.size(),1000));
        u32 latest_version_num;
        bool skip = false;
        R_TRY(yati->GetLatestVersion(cnmt, latest_version_num, skip));
        R_TRY(yati->ShouldSkip(cnmt, skip));

        if (skip) {
            log_write("skipping install!\n");
            if(yati->outcome)++yati->outcome->skipped_records;
            continue;
        }

        R_TRY(yati->ImportTickets(tickets));
        R_TRY(yati->RemoveInstalledNcas(cnmt));
        R_TRY(yati->RegisterNcasAndPushRecord(cnmt, latest_version_num));
        if(yati->outcome)++yati->outcome->installed_records;
    }

    log_write("success!\n");
    R_SUCCEED();
}

} // namespace

namespace {
std::string ExistingUsbPath(const NcmContentId& id){
    u8 digest[32];sha256CalculateHash(digest,&id,sizeof(id));char path[80]{};
    snprintf(path,sizeof(path),"/registered/%08X/%s.nca",unsigned(digest[0]),nexusapp::HexId(&id).c_str());return path;
}
Result ExistingCollection(const ExistingUsbRecord& record,CnmtCollection& cnmt){
    R_UNLESS(record.application_id && record.key.id && record.key.install_type==NcmContentInstallType_Full &&
        record.key.type>=NcmContentMetaType_Application && record.key.type<=NcmContentMetaType_DataPatch &&
        record.installed_meta.size()>=sizeof(NcmContentMetaHeader) && record.installed_meta.size()<=1024*1024 &&
        !record.content.empty() && record.content.size()<=4096,nexus::Invalid);
    cnmt={};cnmt.key=record.key;memcpy(&cnmt.meta_header,record.installed_meta.data(),sizeof(cnmt.meta_header));
    const auto& header=cnmt.meta_header;
    const size_t content_offset=sizeof(header)+header.extended_header_size;
    R_UNLESS(header.content_count==record.content.size() && content_offset<=record.installed_meta.size() &&
        record.content.size()<=(record.installed_meta.size()-content_offset)/sizeof(NcmContentInfo),nexus::Invalid);
    const size_t tail_offset=content_offset+record.content.size()*sizeof(NcmContentInfo);
    R_UNLESS(header.content_meta_count<=(record.installed_meta.size()-tail_offset)/sizeof(NcmContentMetaInfo),nexus::Invalid);
    cnmt.extended_header.assign(record.installed_meta.begin()+sizeof(header),record.installed_meta.begin()+content_offset);
    cnmt.metadata_tail.assign(record.installed_meta.begin()+tail_offset,record.installed_meta.end());
    R_UNLESS(ApplicationId(cnmt)==record.application_id,nexus::Invalid);
    size_t required_tail=size_t(header.content_meta_count)*sizeof(NcmContentMetaInfo);
    if(record.key.type==NcmContentMetaType_Patch){
        R_UNLESS(cnmt.extended_header.size()>=sizeof(NcmPatchMetaExtendedHeader),nexus::Invalid);
        NcmPatchMetaExtendedHeader ext;memcpy(&ext,cnmt.extended_header.data(),sizeof(ext));required_tail+=ext.extended_data_size;
    }else if(record.key.type==NcmContentMetaType_DataPatch){
        R_UNLESS(cnmt.extended_header.size()>=sizeof(NcmDataPatchMetaExtendedHeader),nexus::Invalid);
        NcmDataPatchMetaExtendedHeader ext;memcpy(&ext,cnmt.extended_header.data(),sizeof(ext));required_tail+=ext.extended_data_size;
    }
    R_UNLESS(required_tail==cnmt.metadata_tail.size(),nexus::Invalid);
    for(size_t i=0;i<record.content.size();++i){
        const auto& input=record.content[i];const auto& info=input.info;
        R_UNLESS(!memcmp(&info,record.installed_meta.data()+content_offset+i*sizeof(info),sizeof(info)),nexus::Invalid);
        const NcmContentId zero{};u64 size{};ncmContentInfoSizeToU64(&info,&size);
        R_UNLESS(memcmp(&info.content_id,&zero,sizeof(zero)) && size>=sizeof(nca::Header) && size<=INT64_MAX &&
            (i?info.content_type!=NcmContentType_Meta:info.content_type==NcmContentType_Meta) &&
            info.content_type<NcmContentType_DeltaFragment,nexus::Invalid);
        for(size_t previous=0;previous<i;++previous)
            R_UNLESS(memcmp(&info.content_id,&record.content[previous].info.content_id,sizeof(info.content_id)),nexus::Invalid);
        if(input.has_expected_hash){const std::array<u8,32> zero_hash{};R_UNLESS(input.expected_hash!=zero_hash,nexus::Invalid);}
        NcaCollection entry;entry.name=nexusapp::HexId(&info.content_id)+".nca";entry.size=size;
        entry.content_id=info.content_id;entry.type=info.content_type;entry.header.size=size;entry.skipped=true;
        entry.expected_size=size;entry.has_expected_hash=input.has_expected_hash;memcpy(entry.expected_hash,input.expected_hash.data(),32);
        if(i){cnmt.ncas.push_back(std::move(entry));NcmPackagedContentInfo packed{};packed.info=info;memcpy(packed.hash,input.expected_hash.data(),32);cnmt.infos.push_back(packed);}
        else{static_cast<NcaCollection&>(cnmt)=std::move(entry);cnmt.content_info=info;}
    }
    return 0;
}
Result InspectExisting(ui::ProgressBox* pbox,u64 generation,CnmtCollection& cnmt,const keys::Keys& keys,bool hash_contents,bool require_tickets){
    R_UNLESS(pbox && generation,nexus::Invalid);nexus::Client usb;R_TRY(usb.Check(generation));
    std::vector<FsRightsId> common,personal;
    if(require_tickets){R_TRY(es::GetCommonTickets(common));R_TRY(es::GetPersonalisedTickets(personal));}
    std::vector<u8> buffer(hash_contents?1024*1024:sizeof(nca::Header));
    const auto inspect=[&](NcaCollection& entry)->Result{
        R_TRY(pbox->ShouldExitResult());R_TRY(usb.Check(generation));const auto path=ExistingUsbPath(entry.content_id);
        nexus::Info stat{};R_TRY(usb.Stat(generation,path,stat));R_UNLESS(!stat.directory && stat.size==entry.expected_size,nexus::Invalid);
        u64 got{};R_TRY(usb.Read(generation,path,0,buffer.data(),sizeof(nca::Header),got));R_UNLESS(got==sizeof(nca::Header),nexus::Invalid);
        nca::Header header;memcpy(&header,buffer.data(),sizeof(header));
        if(!header.IsValid())R_TRY(nca::DecryptHeader(buffer.data(),keys,header));
        R_UNLESS(header.size==entry.expected_size && header.kaek_index<3 && header.GetKeyGeneration()<=KEYGEN_LIMIT,nexus::Invalid);
        if(entry.type==NcmContentType_Meta)R_UNLESS(header.content_type==nca::ContentType_Meta,nexus::Invalid);
        if(entry.type==NcmContentType_Program)R_UNLESS(header.content_type==nca::ContentType_Program,nexus::Invalid);
        if(entry.type==NcmContentType_Control)R_UNLESS(header.content_type==nca::ContentType_Control,nexus::Invalid);
        if(require_tickets && es::IsRightsIdValid(header.rights_id))
            R_UNLESS(es::IsRightsIdFound(header.rights_id,common)||es::IsRightsIdFound(header.rights_id,personal),Result_YatiTicketNotFound);
        entry.header=header;
        if(hash_contents){
            nca::ContentHash hash;pbox->NewTransfer("Checking existing USB content");
            for(u64 off=0;off<stat.size;){
                R_TRY(pbox->ShouldExitResult());R_TRY(usb.Check(generation));const auto count=std::min<u64>(buffer.size(),stat.size-off);
                R_TRY(usb.Read(generation,path,off,buffer.data(),count,got));R_UNLESS(got==count,nexus::Invalid);
                hash.Update(buffer.data(),count,keys.header_key);off+=count;pbox->AddTransferBytes(count);pbox->UpdateTransfer(off,stat.size);
            }
            const void* expected=entry.has_expected_hash?static_cast<const void*>(entry.expected_hash):&entry.content_id;
            R_UNLESS(hash.Matches(expected,entry.has_expected_hash?32:16),Result_YatiInvalidNcaSha256);
        }
        return usb.Check(generation);
    };
    R_TRY(inspect(cnmt));for(auto& entry:cnmt.ncas)R_TRY(inspect(entry));return 0;
}
}
Result VerifyExistingUsbRecord(ui::ProgressBox* pbox,u64 generation,const ExistingUsbRecord& record){
    CnmtCollection cnmt;R_TRY(ExistingCollection(record,cnmt));keys::Keys keys;R_TRY(keys::parse_keys(keys,false));
    return InspectExisting(pbox,generation,cnmt,keys,true,false);
}
Result PublishExistingUsbRecord(ui::ProgressBox* pbox,u64 generation,const ExistingUsbRecord& record,u32 latest_version,nexus::install::Diagnostic* diagnostic){
    CnmtCollection cnmt;R_TRY(ExistingCollection(record,cnmt));
    // No source archive is needed: this adapter cannot supply or copy data.
    struct ExistingSource final:source::Base{Result Read(void*,s64,s64,u64* got)override{if(got)*got=0;return nexus::Invalid;}} source;
    Yati yati(pbox,&source);ConfigOverride config;config.nexus_target=nexus::Target::Usb;
    config.nexus_destination_generation=generation;config.diagnostic=diagnostic;config.archive_usb_tickets=false;
    R_TRY(yati.Setup(config));yati.defer_postcommit=true;
    std::vector<ncm::ContentStorageRecord> current;R_TRY(ns::GetApplicationRecords(record.application_id,current));
    for(const auto& existing:current)if(existing.key.id==record.key.id && existing.key.type==record.key.type)
        R_UNLESS(existing.storage_id==NcmStorageId_SdCard && existing.key.version==record.key.version &&
            existing.key.install_type==record.key.install_type,nexus::Invalid);
    R_TRY(InspectExisting(pbox,generation,cnmt,yati.keys,false,true));
    // Reuses PublishNca -> VerifyReusedNca -> generation-pinned P2 registration,
    // NCM readback and NS record merging. No deletion/downgrade cleanup occurs.
    return yati.RegisterNcasAndPushRecord(cnmt,latest_version);
}

namespace {
namespace manager_update=nexus::manager_update;
static_assert(NcmStorageId_BuiltInUser==4 && NcmStorageId_SdCard==5 &&
    NcmContentMetaType_Application==0x80 && NcmContentInstallType_Full==0);
manager_update::Identity ManagerIdentity(const NcmContentMetaKey& key,NcmStorageId storage){
    return {key.id,key.version,key.type,key.install_type,static_cast<u8>(storage)};
}
manager_update::Kind ManagerNcaKind(u8 type){
    switch(type){
        case nca::ContentType_Meta:return manager_update::Kind::Meta;
        case nca::ContentType_Program:return manager_update::Kind::Program;
        case nca::ContentType_Control:return manager_update::Kind::Control;
        default:return manager_update::Kind::Other;
    }
}
manager_update::Kind ManagerContentKind(u8 type){
    switch(type){
        case NcmContentType_Meta:return manager_update::Kind::Meta;
        case NcmContentType_Program:return manager_update::Kind::Program;
        case NcmContentType_Control:return manager_update::Kind::Control;
        default:return manager_update::Kind::Other;
    }
}
manager_update::Content ManagerContent(const NcmContentId& id,const nca::Header& header,unsigned offset=0){
    manager_update::Content content;
    memcpy(content.id.data(),&id,content.id.size());content.owner=header.program_id;content.size=header.size;
    content.kind=ManagerNcaKind(header.content_type);content.offset=offset;
    const FsRightsId empty{};content.rights_free=!memcmp(&header.rights_id,&empty,sizeof(empty));
    return content;
}
Result ManagerReadMetadata(NcmContentMetaDatabase& db,const NcmContentMetaKey& key,manager_update::Bytes& out){
    out.clear();u64 size{},read{};R_TRY(ncmContentMetaDatabaseGetSize(&db,&size,&key));
    R_UNLESS(size>=sizeof(NcmContentMetaHeader) && size<=manager_update::MaxMetadata,nexus::Invalid);
    out.resize(size);R_TRY(ncmContentMetaDatabaseGet(&db,&key,&read,out.data(),out.size()));
    R_UNLESS(read==out.size(),nexus::Invalid);return 0;
}
Result ManagerCheckRouting(std::span<const manager_update::Content> installed){
    fs::FsNativeSd sd;R_TRY(sd.GetFsOpenResult());nexus::repair::Snapshot snapshot;
    for(size_t i=0;i<snapshot.size();++i){
        fs::File file;const auto opened=sd.OpenFile(fs::FsPath(nexus::repair::Live(i)),FsOpenMode_Read,&file);
        if(opened==MAKERESULT(2,1))continue; // Only confirmed absence permits an empty index.
        R_TRY(opened);s64 size{};R_TRY(file.GetSize(&size));
        R_UNLESS(size>=0 && u64(size)<=nexus::repair::MaxFile,nexus::Invalid);
        auto& image=snapshot[i];image.exists=true;image.data.resize(size);u64 read{};
        R_TRY(file.Read(0,image.data.data(),image.data.size(),0,&read));R_UNLESS(read==image.data.size(),nexus::Invalid);
    }
    R_UNLESS(manager_update::CheckRouting(snapshot,installed)==manager_update::Routing::Native,nexus::Invalid);
    return 0;
}
Result ManagerCheckInstalledContent(Yati& yati,const NcmContentMetaKey& key,std::vector<manager_update::Content>& content){
    std::vector<NcmContentInfo> infos;R_TRY(ncm::GetContentInfos(&yati.db,&key,infos));
    R_UNLESS(infos.size()==3,nexus::Invalid);
    content.clear();
    for(const auto& info:infos){
        R_TRY(yati.pbox->ShouldExitResult());R_TRY(yati.destination->Validate());
        bool exists{};R_TRY(ncmContentStorageHas(&yati.cs,&exists,&info.content_id));R_UNLESS(exists,nexus::Invalid);
        s64 size{};u64 expected{};ncmContentInfoSizeToU64(&info,&expected);
        R_TRY(ncmContentStorageGetSizeFromContentId(&yati.cs,&size,&info.content_id));
        R_UNLESS(size>=s64(sizeof(nca::Header)) && u64(size)==expected,nexus::Invalid);
        nca::Header encrypted{},header{};
        R_TRY(ncmContentStorageReadContentIdFile(&yati.cs,&encrypted,sizeof(encrypted),&info.content_id,0));
        R_TRY(nca::DecryptHeader(&encrypted,yati.keys,header));
        R_UNLESS(header.size==expected && ManagerNcaKind(header.content_type)==ManagerContentKind(info.content_type),nexus::Invalid);
        content.push_back(ManagerContent(info.content_id,header,info.id_offset));
    }
    R_UNLESS(manager_update::ValidLayout(content),nexus::Invalid);return 0;
}
Result ManagerVerifyPublishedContent(Yati& yati,const CnmtCollection& cnmt){
    std::vector<const NcaCollection*> content{&cnmt};for(const auto& nca:cnmt.ncas)content.push_back(&nca);
    std::vector<u8> buffer(1024*1024);
    for(const auto* nca:content){
        yati.pbox->NewTransfer("Checking installed Manager content");
        s64 size{};R_TRY(ncmContentStorageGetSizeFromContentId(&yati.cs,&size,&nca->content_id));
        R_UNLESS(size>0 && u64(size)==nca->header.size,nexus::Invalid);
        Sha256Context hash;sha256ContextCreate(&hash);
        for(s64 offset=0;offset<size;){
            R_TRY(yati.pbox->ShouldExitResult());R_TRY(yati.destination->Validate());
            const auto count=std::min<s64>(buffer.size(),size-offset);
            R_TRY(ncmContentStorageReadContentIdFile(&yati.cs,buffer.data(),count,&nca->content_id,offset));
            sha256ContextUpdate(&hash,buffer.data(),count);offset+=count;yati.pbox->UpdateTransferBytes(offset,size);
        }
        u8 digest[32];sha256ContextGetHash(&hash,digest);R_UNLESS(!memcmp(digest,nca->hash,sizeof(digest)),nexus::Invalid);
    }
    return 0;
}
Result ManagerControlPath(NcmContentStorage& storage,const NcaCollection& control,fs::FsPath& path){
    if(control.skipped)return ncmContentStorageGetPath(&storage,path,sizeof(path),&control.content_id);
    return ncmContentStorageGetPlaceHolderPath(&storage,path,sizeof(path),&control.placeholder_id);
}
struct ManagerMetadataTransaction {
    Yati& yati;NcmContentMetaKey key;manager_update::Identity identity;
    const std::vector<manager_update::Content>& installed_content;
    Result error{};std::string backup;
    bool Ok(Result result){if(R_FAILED(result)&&R_SUCCEEDED(error))error=result;return R_SUCCEEDED(result);}
    bool Read(manager_update::Bytes& out){return Ok(ManagerReadMetadata(yati.db,key,out));}
    bool Set(const manager_update::Bytes& data){return Ok(ncmContentMetaDatabaseSet(&yati.db,&key,data.data(),data.size()));}
    bool Commit(){return Ok(ncmContentMetaDatabaseCommit(&yati.db));}
    bool BeforeWrite(){
        std::string detail;
        return Ok(yati.pbox->ShouldExitResult())&&Ok(CheckManagerUpdateStorage(yati.storage_id,detail))&&
            Ok(yati.destination->Validate())&&Ok(ManagerCheckRouting(installed_content));
    }
    bool RecordUnchanged(){
        std::vector<ncm::ContentStorageRecord> records;
        if(!Ok(ns::GetApplicationRecords(manager_update::TitleId,records)))return false;
        if(records.size()!=1)return Ok(nexus::Invalid);
        return Ok(manager_update::SameInstalledApplication(identity,
            ManagerIdentity(records.front().key,static_cast<NcmStorageId>(records.front().storage_id)),records.size())?0:nexus::Invalid);
    }
    bool Backup(const manager_update::Bytes& before,const manager_update::Bytes& after){
        // A bounded, unique recovery snapshot is flushed and read back before
        // changing NCM. Keep it and every old NCA even after successful updates.
        // All new NCAs are already registered. Both database values reference
        // complete content; this does not make a failed system commit atomic.
        manager_update::Bytes bytes{'N','X','M','U','P','D','1',0};
        const auto append=[&](u64 value,unsigned count){for(unsigned i=0;i<count;++i)bytes.push_back(value>>(i*8));};
        append(identity.title,8);append(identity.version,4);append(identity.type,1);append(identity.install_type,1);append(identity.storage,1);append(0,1);
        append(before.size(),4);append(after.size(),4);
        for(const auto* data:{&before,&after}){u8 hash[32];sha256CalculateHash(hash,data->data(),data->size());bytes.insert(bytes.end(),hash,hash+sizeof(hash));}
        bytes.insert(bytes.end(),before.begin(),before.end());bytes.insert(bytes.end(),after.begin(),after.end());
        backup="sdmc:/switch/nexusmanager/updates/metadata-"+std::to_string(armGetSystemTick())+".rollback";
        FILE* file=fopen(backup.c_str(),"wbx");if(!file)return Ok(nexus::Unavailable);
        const auto wrote=fwrite(bytes.data(),1,bytes.size(),file);const auto flushed=fflush(file);
        const auto synced=fsync(fileno(file));const auto closed=fclose(file);
        if(wrote!=bytes.size()||flushed||synced||closed)return Ok(nexus::Unavailable);
        auto* sd=fsdevGetDeviceFileSystem("sdmc:");if(!sd||!Ok(fsFsCommit(sd)))return Ok(nexus::Unavailable);
        file=fopen(backup.c_str(),"rb");if(!file)return Ok(nexus::Unavailable);
        manager_update::Bytes check(bytes.size());const auto got=fread(check.data(),1,check.size(),file);
        const auto extra=fgetc(file);const bool failed=ferror(file);const auto read_closed=fclose(file);
        return Ok(got==bytes.size() && extra==EOF && !failed && !read_closed && check==bytes?0:nexus::Unavailable);
    }
};
}

Result CheckManagerUpdateStorage(NcmStorageId storage,std::string& detail){
    if(storage==NcmStorageId_BuiltInUser)return 0;
    detail="Automatic Manager updates require its app to be installed on SD or internal storage.";
    R_UNLESS(storage==NcmStorageId_SdCard,nexus::Invalid);
    // A disconnected drive can mount between checking status and NCM Set.
    // Old Nexus firmware is therefore refused whenever the provider exists,
    // regardless of current USB readiness. Never infer support from a build label.
    detail="Automatic SD app updates need Nexus U1 or newer firmware with explicit install targets. Update the firmware and reboot, or install Manager in internal storage first. The app was kept.";
    Result detected{};const auto provider=nexus::Detect(detected);
    R_TRY(detected);
    if(provider==nexus::Provider::Absent)return 0;
    if(provider!=nexus::Provider::Present)return nexus::Unavailable;
    nexus::registration::Capabilities caps{};
    R_TRY(serviceDispatch(ncmGetServiceSession(),nexus::registration::CapabilitiesCommand,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{&caps,sizeof(caps)}}));
    R_UNLESS(nexus::registration::ExplicitInstallTargetSupported(caps),nexus::Unavailable);
    return 0;
}

Result InstallManagerUpdateFromLocalNsp(ui::ProgressBox* pbox,fs::Fs* fs,const fs::FsPath& path,
    std::string_view expected_version,std::string_view expected_sha256,u64 expected_size,
    NcmStorageId storage,std::string& detail){
    detail="Manager update package or installation location is not supported. The installed app was kept.";
    R_UNLESS(pbox && fs && manager_update::AllowedStorage(storage) &&
        expected_version.size()<16 && nexus::updates::VersionCode(expected_version) && expected_sha256.size()==64 &&
        !expected_version.starts_with("v") && !expected_version.ends_with("-preview") &&
        path.starts_with("/switch/nexusmanager/updates/") && path.ends_with(".nsp"),nexus::Invalid);
    R_TRY(CheckManagerUpdateStorage(storage,detail));
    source::File source(fs,path);R_TRY(source.GetOpenResult());s64 size{};R_TRY(source.GetSize(&size));
    R_UNLESS(size>0 && size<=64LL*1024*1024 && u64(size)==expected_size,nexus::Invalid);
    // Pin the authenticated bytes to the same open file handle used below.
    // Hash before acquiring installer services or creating any placeholder.
    detail="The saved Manager package no longer matches the verified release. The installed app was kept.";
    pbox->NewTransfer("Verifying Manager package");
    Sha256Context package_hash;sha256ContextCreate(&package_hash);std::vector<u8> buffer(1024*1024);
    for(s64 offset=0;offset<size;){
        R_TRY(pbox->ShouldExitResult());const auto count=std::min<s64>(buffer.size(),size-offset);u64 got{};
        R_TRY(source.Read(buffer.data(),offset,count,&got));R_UNLESS(got==u64(count),nexus::Invalid);
        sha256ContextUpdate(&package_hash,buffer.data(),count);offset+=count;pbox->UpdateTransferBytes(offset,size);
    }
    std::array<u8,32> package_digest{};sha256ContextGetHash(&package_hash,package_digest.data());
    R_UNLESS(manager_update::DigestMatches(package_digest,expected_sha256),nexus::Invalid);
    container::Nsp package(&source,size);container::Collections listed,collections;
    R_TRY(package.GetCollections(listed));R_UNLESS(nexus::archive::Prepare(listed,collections)&&collections.size()==3,nexus::Invalid);
    const container::CollectionEntry* metadata{};
    for(const auto& entry:collections){
        NcmContentId id{};R_UNLESS(entry.size>=s64(sizeof(nca::Header)) &&
            nexus::compat::ContentName(entry.name,&id,false),nexus::Invalid);
        if(entry.name.ends_with(".cnmt.nca")){R_UNLESS(!metadata,nexus::Invalid);metadata=&entry;}
    }
    R_UNLESS(metadata,nexus::Invalid);
    ConfigOverride config{};config.nexus_target=storage==NcmStorageId_BuiltInUser?nexus::Target::Internal:nexus::Target::Sd;
    config.sd_card_install=storage==NcmStorageId_SdCard;
    config.ticket_only=false;config.skip_base=false;config.skip_patch=true;config.skip_addon=true;config.skip_data_patch=true;config.skip_ticket=true;
    config.skip_nca_hash_verify=false;config.skip_rsa_header_fixed_key_verify=true;config.skip_rsa_npdm_fixed_key_verify=true;
    config.ignore_distribution_bit=true;config.convert_to_common_ticket=false;config.convert_to_standard_crypto=false;
    config.lower_master_key=false;config.lower_system_version=false;config.archive_usb_tickets=false;
    Yati yati(pbox,&source);R_TRY(yati.Setup(config));
    // Do not let installer preferences turn this verified replacement into a
    // skipped operation. Homebrew NCAs are unsigned; their full hashes are mandatory.
    yati.config.skip_if_already_installed=false;
    std::vector<ncm::ContentStorageRecord> records;R_TRY(ns::GetApplicationRecords(manager_update::TitleId,records));
    R_UNLESS(records.size()==1,nexus::Invalid);
    const auto installed=ManagerIdentity(records.front().key,static_cast<NcmStorageId>(records.front().storage_id));
    auto expected=installed;expected.storage=static_cast<u8>(storage);
    R_UNLESS(manager_update::SameInstalledApplication(installed,expected,records.size()),nexus::Invalid);
    detail="The existing Manager installation could not be verified on its original storage. No app records were changed.";
    manager_update::Bytes before;R_TRY(ManagerReadMetadata(yati.db,records.front().key,before));
    std::vector<manager_update::Content> installed_content;R_TRY(ManagerCheckInstalledContent(yati,records.front().key,installed_content));
    detail="Automatic updates require Manager on SD or internal storage without USB routing. Install Manager there first. Its USB routing indexes could not be ruled out; no app records were changed.";
    R_TRY(ManagerCheckRouting(installed_content));
    detail="The update does not contain exactly the expected Manager application. No app records were changed.";
    std::vector<manager_update::Content> layout;
    for(const auto& entry:collections){
        nca::Header encrypted{},header{};R_TRY(yati.ReadSourceExact(&encrypted,entry.offset,sizeof(encrypted)));
        R_TRY(nca::DecryptHeader(&encrypted,yati.keys,header));
        R_UNLESS(header.size==u64(entry.size) && header.distribution_type==nca::DistributionType_System,nexus::Invalid);
        NcmContentId id{};R_UNLESS(nexus::compat::ContentName(entry.name,&id,false),nexus::Invalid);
        const auto content=ManagerContent(id,header);layout.push_back(content);
        R_UNLESS((content.kind==manager_update::Kind::Meta)==entry.name.ends_with(".cnmt.nca"),nexus::Invalid);
    }
    R_UNLESS(manager_update::ValidLayout(layout),nexus::Invalid);
    std::vector<TikCollection> tickets;
    CnmtCollection cnmt{NcaCollection{*metadata}};cnmt.type=NcmContentType_Meta;
    ON_SCOPE_EXIT(yati.Cleanup(cnmt);for(const auto& nca:cnmt.ncas)yati.Cleanup(nca););
    R_TRY(yati.InstallCnmtNca(tickets,cnmt,collections));
    R_UNLESS(manager_update::SameInstalledApplication(installed,ManagerIdentity(cnmt.key,storage),records.size()) &&
        cnmt.source_install_type==NcmContentInstallType_Full && cnmt.ncas.size()==2 && cnmt.infos.size()==2 &&
        cnmt.extended_header.size()==sizeof(NcmApplicationMetaExtendedHeader) &&
        !cnmt.meta_header.content_meta_count && cnmt.metadata_tail.empty(),nexus::Invalid);
    // Reject aliases, duplicate references, other content types, and extra
    // program indexes before installing Program/Control placeholders.
    auto remaining=layout;
    std::erase_if(remaining,[](const auto& c){return c.kind==manager_update::Kind::Meta;});
    for(const auto& content:cnmt.ncas){
        R_UNLESS(!content.verified_alias,nexus::Invalid);
        const auto found=std::find_if(remaining.begin(),remaining.end(),[&](const auto& candidate){return
            !memcmp(candidate.id.data(),&content.content_id,candidate.id.size()) && candidate.size==content.expected_size &&
            candidate.kind==ManagerContentKind(content.type);});
        R_UNLESS(found!=remaining.end(),nexus::Invalid);remaining.erase(found);
    }
    for(const auto& info:cnmt.infos)R_UNLESS(info.info.id_offset==0,nexus::Invalid);
    R_UNLESS(remaining.empty(),nexus::Invalid);
    detail="Manager update content could not be installed and verified. The previous app records were kept; the verified NSP is still on SD.";
    for(auto& content:cnmt.ncas)R_TRY(yati.InstallNca(tickets,content));
    const auto control=std::find_if(cnmt.ncas.begin(),cnmt.ncas.end(),[](const auto& nca){return nca.type==NcmContentType_Control;});
    R_UNLESS(control!=cnmt.ncas.end(),nexus::Invalid);
    fs::FsPath control_path;R_TRY(ManagerControlPath(yati.cs,*control,control_path));
    {
        fs::FsNativeId control_fs{manager_update::TitleId,FsFileSystemType_ContentControl,control_path,FsContentAttributes_All};
        R_TRY(control_fs.GetFsOpenResult());fs::File nacp;R_TRY(control_fs.OpenFile("/control.nacp",FsOpenMode_Read,&nacp));
        s64 nacp_size{};R_TRY(nacp.GetSize(&nacp_size));R_UNLESS(nacp_size==0x4000,nexus::Invalid);
        std::array<char,16> display{};u64 read{};R_TRY(nacp.Read(0x3060,display.data(),display.size(),0,&read));
        R_UNLESS(read==display.size() && manager_update::DisplayVersionMatches(display,expected_version),nexus::Invalid);
    }
    // The full source hashes have passed. Publish every new NCA before changing
    // the single database value; old content and the existing NS event remain.
    R_TRY(yati.PublishNcas(cnmt));
    R_TRY(ManagerVerifyPublishedContent(yati,cnmt));
    BufHelper after;after.write(&cnmt.meta_header,sizeof(cnmt.meta_header));
    after.write(cnmt.extended_header.data(),cnmt.extended_header.size());after.write(&cnmt.content_info,sizeof(cnmt.content_info));
    for(const auto& info:cnmt.infos)after.write(&info.info,sizeof(info.info));
    manager_update::Bytes next(after.buf.begin(),after.buf.begin()+after.tell());
    ManagerMetadataTransaction transaction{yati,cnmt.key,installed,installed_content};
    pbox->NewTransfer("Finishing Manager update");
    const auto result=manager_update::ReplaceMetadata(transaction,before,next);
    if(result==manager_update::Publication::Installed){
        detail="Manager update installed and verified. The previous app content and a metadata backup were kept. The verified NSP remains on SD.";
        return 0;
    }
    if(result==manager_update::Publication::RestoreFailed)
        detail="Manager update could not restore its previous app metadata. Keep the verified NSP and metadata backup on SD. Use an installer to repair Manager before reopening it.";
    else if(result==manager_update::Publication::Restored)
        detail="Manager update stopped and its previous app metadata was restored and verified. The old app content and verified NSP were kept.";
    else detail="Manager update stopped before app metadata changed. The previous app and verified NSP were kept.";
    if(!transaction.backup.empty())detail+="\n\nMetadata backup: "+transaction.backup.substr(5);
    return R_FAILED(transaction.error)?transaction.error:nexus::Invalid;
}

Result InstallFromFile(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& path, const ConfigOverride& override) {
    if(override.diagnostic)override.diagnostic->Clear();
    auto source = std::make_unique<source::File>(fs, path);
    const auto open_result=source->GetOpenResult();
    if(R_FAILED(open_result) && override.diagnostic && nexus::package::SplitPart(path.s))
        override.diagnostic->text="Split XCI could not be opened. Keep every numbered part in the same folder, choose the first part (.xc0 or .xci.00), and check that no parts are missing or empty. The original files were not changed.";
    R_TRY(open_result);
    s64 source_size{-1};
    if (R_FAILED(source->GetSize(&source_size))) {
        source_size = -1;
    }
    // auto source = std::make_unique<source::StreamFile>(fs, path, override); // enable for testing.
    return InstallFromSource(pbox, source.get(), path, override, source_size);
}

Result InstallFromSource(ui::ProgressBox* pbox, source::Base* source, const fs::FsPath& path, const ConfigOverride& override, s64 source_size) {
    if(override.diagnostic)override.diagnostic->Clear();
    const auto ext = std::strrchr(path.s, '.');
    R_UNLESS(ext, Result_YatiContainerNotFound);

    if (!strcasecmp(ext, ".msp")) {
        return msp::Install(pbox, source, source_size);
    }

    std::unique_ptr<container::Base> container;
    if(nexus::package::SplitPart(path.s) && !source->HasJoinedParts()) {
        if(override.diagnostic)override.diagnostic->text="This is one part of a split XCI. Put all parts in the same folder on SD or USB and choose the first part in Install games. A single part cannot be streamed as a complete game.";
        return Result_XciInvalidHeader;
    }
    const auto kind = nexus::package::Classify(path.s);
    if (kind == nexus::package::Kind::Nsp || kind == nexus::package::Kind::Nsz) {
        container = std::make_unique<container::Nsp>(source, source_size);
    } else if (kind == nexus::package::Kind::Xci || kind == nexus::package::Kind::Xcz) {
        container = std::make_unique<container::Xci>(source, source_size);
    }

    R_UNLESS(container, Result_YatiContainerNotFound);
    return InstallFromContainer(pbox, container.get(), override);
}

Result InstallFromContainer(ui::ProgressBox* pbox, container::Base* container, const ConfigOverride& override) {
    if(override.diagnostic)override.diagnostic->Clear();
    container::Collections collections;
    const auto parse_rc=container->GetCollections(collections);
    if(R_FAILED(parse_rc) && override.diagnostic)override.diagnostic->Set(nexus::install::Failure::Package);
    R_TRY(parse_rc);
    return InstallFromCollections(pbox, container->GetSource(), collections, override);
}

Result InstallFromCollections(ui::ProgressBox* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override) {
    if(override.diagnostic)override.diagnostic->Clear();
    if(override.outcome)*override.outcome={};
    // Normalize only exact content-ID archive names in this private copy.
    // Source bytes/offsets are unchanged. Reject case aliases before any writes.
    container::Collections prepared;
    if(!nexus::archive::Prepare(collections,prepared)) {
        if(override.diagnostic)override.diagnostic->Set(nexus::install::Failure::Package);
        return Result_NspInvalidHeader;
    }
    // Previously an empty/unrecognized CNMT list could return success after
    // doing nothing. Reject before services, placeholders or tickets change.
    if(!nexus::install::HasRecognizedMetadata(prepared)) {
        if(override.diagnostic)override.diagnostic->Set(nexus::install::Failure::Metadata);
        return Result_YatiNcaNotFound;
    }
    if (source->IsStream()) {
        return InstallInternalStream(pbox, source, prepared, override);
    } else {
        return InstallInternal(pbox, source, prepared, override);
    }
}

} // namespace sphaira::yati
