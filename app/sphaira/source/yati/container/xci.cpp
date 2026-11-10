#include "yati/container/xci.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <cstring>
#include <limits>

namespace sphaira::yati::container {
namespace {

#define XCI_MAGIC std::byteswap(0x48454144)
#define HFS0_MAGIC 0x30534648
#define HFS0_ROOT_HEADER_OFFSET 0xF000
#define HFS0_ROOT_HEADER_OFFSET_WITH_KEY_AREA (HFS0_ROOT_HEADER_OFFSET + 0x1000)

constexpr u32 MaxFiles = 0x10000;
constexpr u32 MaxStrings = 16 * 1024 * 1024;
bool Add(u64 a, u64 b, u64& out) {
    if (b > std::numeric_limits<u64>::max() - a) return false;
    out = a + b; return true;
}
Result ReadExact(source::Base* source, void* data, s64 offset, s64 size) {
    return source::ReadExact(*source, data, offset, size, Result_XciInvalidHeader);
}

} // namespace

auto Xci::Hfs0::GetHfs0Data() const -> std::vector<u8> {
    s64 off = 0;
    std::vector<u8> out(GetHfs0Size());

    std::memcpy(out.data() + off, &header, sizeof(header));
    off += sizeof(header);

    std::memcpy(out.data() + off, file_table.data(), file_table.size() * sizeof(Hfs0FileTableEntry));
    off += file_table.size() * sizeof(Hfs0FileTableEntry);

    for (auto& str : string_table) {
        std::memcpy(out.data() + off, str.data(), str.length());
        off += str.length() + 1;
    }

    return out;
}

Result Xci::ReadRootHeader(Root& out) {
    out = {};
    s64 offset = HFS0_ROOT_HEADER_OFFSET;
    const auto read = [&]() -> Result {
        R_UNLESS(m_source_size < 0 || offset <= m_source_size, Result_XciInvalidHeader);
        return Hfs0GetPartition(m_source, offset, out.hfs0, m_source_size < 0 ? -1 : m_source_size-offset);
    };
    auto rc = read();
    if (rc == Result_XciBadMagic) {
        // Dumps may contain a prepended 0x1000-byte key area.
        offset = HFS0_ROOT_HEADER_OFFSET_WITH_KEY_AREA;
        rc = read();
    }
    R_TRY(rc);
    out.hfs0_offset = offset;
    R_SUCCEED();
}

Result Xci::GetRoot(Root& out) {
    R_TRY(ReadRootHeader(out));
    for (u32 i = 0; i < out.hfs0.header.total_files; i++) {
        Partition partition{};
        partition.name = out.hfs0.string_table[i];
        R_TRY(ReadPartitionFromHfs0(m_source, out.hfs0, i, partition));
        out.partitions.emplace_back(std::move(partition));
    }
    R_SUCCEED();
}

Result Xci::GetCollections(Collections& out) {
    out.clear();
    Root root;
    R_TRY(ReadRootHeader(root));
    log_write("[XCI] got root partition\n");
    // Only secure contains installable game content. Do not interpret the
    // cartridge's separate firmware-update partition as a game package.
    u32 found = root.hfs0.header.total_files;
    for (u32 i = 0; i < root.hfs0.header.total_files; ++i) {
        if (root.hfs0.string_table[i] == "secure") {
            R_UNLESS(found == root.hfs0.header.total_files, Result_XciInvalidHeader);
            found = i;
        }
    }
    R_UNLESS(found < root.hfs0.header.total_files, Result_XciSecurePartitionNotFound);
    Partition partition{};
    partition.name = "secure";
    R_TRY(ReadPartitionFromHfs0(m_source, root.hfs0, found, partition));
    out = std::move(partition.collections);
    R_SUCCEED();
}

Result Xci::Hfs0GetPartition(source::Base* source, s64 off, Hfs0& out, s64 container_size) {
    out = {};
    R_UNLESS(off >= 0, Result_XciInvalidHeader);
    u64 limit{};
    if (container_size >= 0) {
        R_UNLESS(Add(off, container_size, limit) && limit <= static_cast<u64>(INT64_MAX), Result_XciInvalidHeader);
        R_UNLESS(container_size >= static_cast<s64>(sizeof(out.header)), Result_XciInvalidHeader);
    }
    R_TRY(ReadExact(source, &out.header, off, sizeof(out.header)));
    log_write("checking magic: %X vs %X\n", out.header.magic, HFS0_MAGIC);
    R_UNLESS(out.header.magic == HFS0_MAGIC, Result_XciBadMagic);
    R_UNLESS(out.header.total_files <= MaxFiles && out.header.string_table_size <= MaxStrings, Result_XciInvalidHeader);
    const u64 table_size = static_cast<u64>(out.header.total_files) * sizeof(Hfs0FileTableEntry);
    u64 table_off{}, strings_off{}, data_off{};
    R_UNLESS(Add(off, sizeof(out.header), table_off) && Add(table_off, table_size, strings_off) &&
        Add(strings_off, out.header.string_table_size, data_off) && data_off <= static_cast<u64>(INT64_MAX), Result_XciInvalidHeader);
    R_UNLESS(container_size < 0 || data_off <= limit, Result_XciInvalidHeader);
    out.file_table.resize(out.header.total_files);
    R_TRY(ReadExact(source, out.file_table.data(), table_off, table_size));
    std::vector<char> names(out.header.string_table_size);
    R_TRY(ReadExact(source, names.data(), strings_off, names.size()));
    for (const auto& file : out.file_table) {
        R_UNLESS(file.name_offset < names.size(), Result_XciInvalidHeader);
        const char* name = names.data() + file.name_offset;
        R_UNLESS(*name && std::memchr(name, '\0', names.size()-file.name_offset), Result_XciInvalidHeader);
        u64 begin{}, end{};
        R_UNLESS(Add(data_off, file.data_offset, begin) && Add(begin, file.data_size, end) &&
            end <= static_cast<u64>(INT64_MAX), Result_XciInvalidHeader);
        R_UNLESS(container_size < 0 || end <= limit, Result_XciInvalidHeader);
        out.string_table.emplace_back(name);
    }
    out.data_offset = data_off;
    R_SUCCEED();
}

Result Xci::ReadPartitionFromHfs0(source::Base* source, const Hfs0& root, u32 index, Partition& out) {
    R_UNLESS(index < root.file_table.size() && index < root.string_table.size(), Result_XciInvalidHeader);
    log_write("[XCI] fetching %s partition\n", root.string_table[index].c_str());
    out.hfs0_offset = root.data_offset + root.file_table[index].data_offset;
    out.hfs0_size = root.file_table[index].data_size;

    out.collections.clear();
    R_TRY(Hfs0GetPartition(source, out.hfs0_offset, out.hfs0, out.hfs0_size));
    log_write("[XCI] got %s partition\n", root.string_table[index].c_str());

    for (u32 i = 0; i < out.hfs0.header.total_files; i++) {
        CollectionEntry entry;
        entry.name = out.hfs0.string_table[i];
        entry.offset = out.hfs0.data_offset + out.hfs0.file_table[i].data_offset;
        entry.size = out.hfs0.file_table[i].data_size;
        out.collections.emplace_back(entry);
    }

    log_write("[XCI] read %s partition count: %zu\n", root.string_table[index].c_str(), out.collections.size());
    R_SUCCEED();
}

} // namespace sphaira::yati::container
