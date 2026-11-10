// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstring>
#include "yati/nx/crypto.hpp"

namespace sphaira::nca {
// Hash the original reconstructed NCA BEFORE installer header edits. A second
// digest reverses only the System/GameCard distribution byte in either direction. Neither
// digest changes the source/output, and EVERY body byte participates in both.
class ContentHash {
public:
    ContentHash() { sha256ContextCreate(&raw); sha256ContextCreate(&original); sha256ContextCreate(&plaintext); }
    void EnablePlaintextOrigin() { allow_plaintext=true; }
    void Update(const void* data, size_t count, const u8* header_key = nullptr) {
        const auto* bytes = static_cast<const u8*>(data);
        sha256ContextUpdate(&raw, bytes, count);
        if (!length && allow_plaintext && count>=0xc00 && header_key) {
            std::array<u8,0xc00> header{};
            crypto::cryptoAes128Xts(bytes,header.data(),header_key,0,0x200,header.size(),false);
            u64 size{};std::memcpy(&size,header.data()+0x208,8);
            // Only the small Control alias representation emitted by this
            // installer; no arbitrary header reconstruction or crypto changes.
            if (!memcmp(header.data()+0x200,"NCA3",4) && header[0x204]==0 && header[0x205]==2 && size>=0xc00 && size<=16*1024*1024) {
                plaintext_size=size;has_plaintext=true;
                sha256ContextUpdate(&plaintext,header.data(),header.size());
                sha256ContextUpdate(&plaintext,bytes+header.size(),count-header.size());
            }
        } else if (has_plaintext) sha256ContextUpdate(&plaintext,bytes,count);
        if (!length && count >= 0x400 && header_key) {
            std::array<u8,0x200> sector{};
            crypto::cryptoAes128Xts(bytes + 0x200, sector.data(), header_key, 1, 0x200, 0x200, false);
            u32 magic{}; u64 size{};
            std::memcpy(&magic, sector.data(), sizeof(magic));
            std::memcpy(&size, sector.data()+8, sizeof(size));
            if (magic == 0x3341434e && sector[4] <= 1 && size >= 0xc00) {
                declared_size = size;
                sector[4] ^= 1;
                crypto::cryptoAes128Xts(sector.data(), sector.data(), header_key, 1, 0x200, 0x200, true);
                sha256ContextUpdate(&original, bytes, 0x200);
                sha256ContextUpdate(&original, sector.data(), sector.size());
                sha256ContextUpdate(&original, bytes+0x400, count-0x400);
                alternate = true;
            }
        } else if (alternate) {
            sha256ContextUpdate(&original, bytes, count);
        }
        length += count;
    }
    void Raw(void* out) { sha256ContextGetHash(&raw, out); }
    bool Matches(const void* expected, size_t count, bool* converted = nullptr, bool* plaintext_origin = nullptr) {
        if (converted) *converted = false;
        if (plaintext_origin) *plaintext_origin = false;
        if (count != 16 && count != 32) return false;
        std::array<u8,32> digest{}; Raw(digest.data());
        if (!std::memcmp(digest.data(), expected, count)) return true;
        if (alternate && length==declared_size) {
            sha256ContextGetHash(&original,digest.data());
            if (!std::memcmp(digest.data(),expected,count)) { if(converted)*converted=true;return true; }
        }
        if (has_plaintext && length==plaintext_size) {
            sha256ContextGetHash(&plaintext,digest.data());
            if (!std::memcmp(digest.data(),expected,count)) { if(plaintext_origin)*plaintext_origin=true;return true; }
        }
        return false;
    }
private:
    Sha256Context raw{}, original{}, plaintext{};
    u64 length{}, declared_size{}, plaintext_size{};
    bool alternate{}, allow_plaintext{}, has_plaintext{};
};
}
