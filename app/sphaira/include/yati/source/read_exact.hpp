#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sphaira::yati::source {

// Fill a bounded request using only successful, positive-progress reads.
// Never retry an error, rewind a stream, or pretend EOF filled the buffer.
template<class Reader, class Check>
std::uint32_t ReadExact(Reader& reader, void* data, std::int64_t offset,
    std::int64_t size, std::uint32_t invalid_read, Check check) {
    if (offset < 0 || size < 0 || offset > INT64_MAX - size ||
        static_cast<std::uint64_t>(size) > std::numeric_limits<std::size_t>::max() ||
        (size && !data)) return invalid_read;
    auto* out = static_cast<unsigned char*>(data);
    while (size) {
        const auto guard = check();
        if (guard) return guard;
        std::uint64_t got{};
        const auto rc = reader.Read(out, offset, size, &got);
        if (rc) return rc;
        if (!got || got > static_cast<std::uint64_t>(size)) return invalid_read;
        out += static_cast<std::size_t>(got);
        offset += static_cast<std::int64_t>(got);
        size -= static_cast<std::int64_t>(got);
    }
    return 0;
}

template<class Reader>
std::uint32_t ReadExact(Reader& reader, void* data, std::int64_t offset,
    std::int64_t size, std::uint32_t invalid_read) {
    return ReadExact(reader, data, offset, size, invalid_read, [] { return 0u; });
}

} // namespace sphaira::yati::source
