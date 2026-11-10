#include "yati/container/nsp.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "nexus/content_compat_core.hpp"
#include <algorithm>
#include <map>
#include <array>
#include <limits>
#include <memory>
#include <cstring>

namespace sphaira::yati::container {
namespace {

#define PFS0_MAGIC 0x30534650

struct Pfs0Header {
    u32 magic;
    u32 total_files;
    u32 string_table_size;
    u32 padding;
};

struct Pfs0FileTableEntry {
    u64 data_offset;
    u64 data_size;
    u32 name_offset;
    u32 padding;
};

constexpr u32 MAX_PFS0_FILES = 0x10000;
constexpr u32 MAX_PFS0_STRING_TABLE_SIZE = 16 * 1024 * 1024;
// Only redundant certificate blobs are eligible; tickets/NCAs never are.
constexpr u64 MAX_DUPLICATE_CERT_SIZE = 16 * 1024;
constexpr u64 MAX_DUPLICATE_CERT_READS = 256 * 1024;

bool IsCertificateName(std::string_view name) {
    if (name.size() != 37 || name.substr(32) != ".cert") return false;
    for (size_t i=0; i<32; ++i)
        if (!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f'))) return false;
    return true;
}

bool CheckedAdd(u64 lhs, u64 rhs, u64& out) {
    if (rhs > std::numeric_limits<u64>::max() - lhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

Result ReadExact(source::Base* source, void* data, s64 offset, s64 size) {
    return source::ReadExact(*source, data, offset, size, Result_NspInvalidHeader);
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

    std::vector<u8> buf;
    u64 offset{};
};

} // namespace

Result Nsp::GetCollections(Collections& out) {
    return GetCollections(out, 0, m_source_size);
}

Result Nsp::GetCollections(Collections& out, s64 off) {
    return GetCollections(out, off, -1);
}

Result Nsp::GetCollections(Collections& out, s64 off, s64 container_size) {
    out.clear();
    R_UNLESS(off >= 0, Result_NspInvalidHeader);
    u64 container_end{};
    if (container_size >= 0) {
        R_UNLESS(container_size >= static_cast<s64>(sizeof(Pfs0Header)), Result_NspInvalidHeader);
        R_UNLESS(CheckedAdd(static_cast<u64>(off), static_cast<u64>(container_size), container_end), Result_NspInvalidHeader);
        R_UNLESS(container_end <= static_cast<u64>(std::numeric_limits<s64>::max()), Result_NspInvalidHeader);
    }

    Pfs0Header header{};
    R_TRY(ReadExact(m_source, std::addressof(header), off, sizeof(header)));
    R_UNLESS(header.magic == PFS0_MAGIC, Result_NspBadMagic);
    R_UNLESS(header.total_files <= MAX_PFS0_FILES, Result_NspInvalidHeader);
    R_UNLESS(header.string_table_size <= MAX_PFS0_STRING_TABLE_SIZE, Result_NspInvalidHeader);

    const auto table_size = static_cast<u64>(header.total_files) * sizeof(Pfs0FileTableEntry);
    u64 table_offset{};
    u64 strings_offset{};
    u64 data_offset{};
    R_UNLESS(CheckedAdd(static_cast<u64>(off), sizeof(header), table_offset), Result_NspInvalidHeader);
    R_UNLESS(CheckedAdd(table_offset, table_size, strings_offset), Result_NspInvalidHeader);
    R_UNLESS(CheckedAdd(strings_offset, header.string_table_size, data_offset), Result_NspInvalidHeader);
    R_UNLESS(data_offset <= static_cast<u64>(std::numeric_limits<s64>::max()), Result_NspInvalidHeader);
    R_UNLESS(container_size < 0 || data_offset <= container_end, Result_NspInvalidHeader);

    std::vector<Pfs0FileTableEntry> file_table(header.total_files);
    R_TRY(ReadExact(m_source, file_table.data(), table_offset, table_size));

    std::vector<char> string_table(header.string_table_size);
    R_TRY(ReadExact(m_source, string_table.data(), strings_offset, string_table.size()));

    u32 highest_name{};
    size_t highest_count{};
    for (const auto& file : file_table) {
        if (file.name_offset > highest_name) { highest_name=file.name_offset; highest_count=0; }
        if (file.name_offset == highest_name) ++highest_count;
    }
    Collections parsed;
    parsed.reserve(header.total_files);
    std::map<std::string,size_t> names;
    std::vector<std::pair<size_t,size_t>> duplicates;
    u64 duplicate_reads{};
    std::vector<std::pair<u64,u64>> spans;
    bool used_bounded_name=false;
    for (size_t i=0; i<file_table.size(); ++i) {
        const auto& file=file_table[i];
        std::string_view name;
        bool unterminated{};
        const bool final_unique_highest=container_size>=0 && i+1==file_table.size()
            && file.name_offset==highest_name && highest_count==1;
        R_UNLESS(nexus::compat::PfsName(std::string_view(string_table.data(),string_table.size()),
            file.name_offset,final_unique_highest,name,unterminated),Result_NspInvalidHeader);
        used_bounded_name |= unterminated;
        std::string folded(name);
        for (auto& c:folded) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        const auto [existing,unique] = names.emplace(folded,i);
        if (!unique) {
            // Unknown container sizes cannot establish safe bounds for extra reads.
            R_UNLESS(container_size>=0 && IsCertificateName(folded),Result_NspInvalidHeader);
            const auto& original=file_table[existing->second];
            R_UNLESS(file.data_size>0 && file.data_size<=MAX_DUPLICATE_CERT_SIZE
                && file.data_size==original.data_size,Result_NspInvalidHeader);
            duplicate_reads += 2*file.data_size; // Each entry is bounded above.
            R_UNLESS(duplicate_reads<=MAX_DUPLICATE_CERT_READS,Result_NspInvalidHeader);
            duplicates.emplace_back(existing->second,i);
        }

        u64 file_offset{};
        u64 file_end{};
        R_UNLESS(CheckedAdd(data_offset, file.data_offset, file_offset), Result_NspInvalidHeader);
        R_UNLESS(CheckedAdd(file_offset, file.data_size, file_end), Result_NspInvalidHeader);
        R_UNLESS(file_end <= static_cast<u64>(std::numeric_limits<s64>::max()), Result_NspInvalidHeader);
        R_UNLESS(container_size < 0 || file_end <= container_end, Result_NspInvalidHeader);
        if (file.data_size) spans.emplace_back(file_offset,file_end);

        CollectionEntry entry;
        entry.name.assign(name.data(),name.size());
        entry.offset = file_offset;
        entry.size = file.data_size;
        parsed.emplace_back(std::move(entry));
    }
    std::sort(spans.begin(),spans.end());
    for(size_t i=1;i<spans.size();++i) R_UNLESS(spans[i-1].second<=spans[i].first,Result_NspInvalidHeader);
    // Validate EVERY entry/range before comparing payloads or publishing anything.
    // Full byte equality, not merely equal names, sizes or a short hash prefix.
    std::array<u8,4096> first{},second{};
    std::vector<bool> redundant(parsed.size(),false);
    for (const auto& [original,duplicate] : duplicates) {
        const auto& a=parsed[original];
        const auto& b=parsed[duplicate];
        for (s64 pos=0; pos<a.size;) {
            const auto count=std::min<s64>(first.size(),a.size-pos);
            R_TRY(ReadExact(m_source,first.data(),a.offset+pos,count));
            R_TRY(ReadExact(m_source,second.data(),b.offset+pos,count));
            R_UNLESS(std::memcmp(first.data(),second.data(),count)==0,Result_NspInvalidHeader);
            pos+=count;
        }
        redundant[duplicate]=true;
    }
    if (!duplicates.empty()) {
        Collections unique;
        unique.reserve(parsed.size()-duplicates.size());
        for (size_t i=0;i<parsed.size();++i) if (!redundant[i]) unique.emplace_back(std::move(parsed[i]));
        parsed.swap(unique);
    }
    out.swap(parsed);
    m_data_offset=data_offset;
    if(!duplicates.empty()) log_write("C5: verified identical certificate duplicates skipped=%u\n",static_cast<unsigned>(duplicates.size()));
    if(used_bounded_name) log_write("C3: bounded final PFS archive filename accepted; payload offsets unchanged\n");
    R_SUCCEED();
}

auto Nsp::Build(std::span<const CollectionEntry> entries, s64& size) -> std::vector<u8> {
    BufHelper buf;

    Pfs0Header header{};
    std::vector<Pfs0FileTableEntry> file_table(entries.size());
    std::vector<char> string_table;

    u64 string_offset{};
    u64 data_offset{};

    for (u32 i = 0; i < entries.size(); i++) {
        file_table[i].data_offset = data_offset;
        file_table[i].data_size = entries[i].size;
        file_table[i].name_offset = string_offset;
        file_table[i].padding = 0;

        string_table.resize(string_offset + entries[i].name.length() + 1);
        std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);

        data_offset += file_table[i].data_size;
        string_offset += entries[i].name.length() + 1;
    }

    // Add padding to the string table so that the header as a whole is well-aligned
    const auto nameless_header_size = sizeof(Pfs0Header) + (file_table.size() * sizeof(Pfs0FileTableEntry));
    auto padded_string_table_size = ((nameless_header_size + string_table.size() + 0x1F) & ~0x1F) - nameless_header_size;

    // Add manual padding if the full Partition FS header would already be properly aligned.
    if (padded_string_table_size == string_table.size()) {
        padded_string_table_size += 0x20;
    }

    string_table.resize(padded_string_table_size);

    header.magic = PFS0_MAGIC;
    header.total_files = entries.size();
    header.string_table_size = string_table.size();
    header.padding = 0;

    buf.write(&header, sizeof(header));
    buf.write(file_table.data(), sizeof(Pfs0FileTableEntry) * file_table.size());
    buf.write(string_table.data(), string_table.size());

    // calculate nsp size.
    size = buf.tell();
    for (const auto& e : file_table) {
        size += e.data_size;
    }

    return buf.buf;
}

} // namespace sphaira::yati::container
