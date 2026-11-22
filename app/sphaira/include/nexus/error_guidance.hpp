// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <cstdint>
#include "nexus/registration_protocol.hpp"

namespace nexus::diagnostics {
struct Guidance { const char* title; const char* help; };
// Decode exact FS results, not generic "corrupt" ranges. Unknown results retain
// their original text/code. This is presentation only: no Result is rewritten.
constexpr Guidance Explain(std::uint32_t result) {
    if(result==registration::RepairPending)return {"USB library recovery is pending"_nx,
        "USB changes are blocked because an earlier library change is unfinished. Fully restart. For an interrupted import, use Help & options > Import USB games into this system to restore its backup; otherwise use Repair USB library. Keep the import and repair backup files."_nx};
    // Official SF NotSupported / CMIF UnknownCommandId. Preserve the raw code.
    if(result==(10u|(221u<<9)) || result==(10u|(1u<<9)))return {"Required firmware command unavailable"_nx,
        "USB Loader 0.7.17 P2 requires matching P2 firmware for USB content registration. Install the matching firmware and fully reboot, then retry. This operation failed; verified bytes alone do not confirm USB registration. Keep the original source and exact error/log."_nx};
    if ((result & 0x1ffu) != 2) return {};
    switch ((result >> 9) & 0x1fffu) {
        case 4518:
            return {"NCA header signature rejected"_nx,
                "The reader rejected an NCA header signature. This is not a game-data hash failure.\n\nFor a USB game, install the matching SC1 firmware and fully reboot; updating USB Loader alone is not enough. SC1 has built-in USB application-header compatibility.\n\nIf it persists, keep the exact error and fresh usb_loader_trace.txt. Do not format the drive or repeatedly reinstall the same package."_nx};
        case 4519:
            return {"NCA secondary signature rejected"_nx,
                "The NCA secondary signature was rejected. The USB header-compatibility setting does not disable this separate check.\n\nKeep the exact error and fresh USB launch log so the failing stage can be checked. Index recovery does not repair signatures."_nx};
        case 1004: case 6451: case 6452:
            return {"NCA content key unavailable or inconsistent"_nx,
                "The reader could not use the required content key. This is separate from file corruption.\n\nCheck that this game's legitimate ticket/content installation and matching firmware are intact. Index recovery cannot recreate missing tickets or keys. Never share keys or tickets in a support log."_nx};
        case 4520: case 4549: case 4612: case 4613: case 4643:
            return {"NCA integrity verification failed"_nx,
                "A content/header/metadata integrity check failed. Damaged or modified bytes, or a reader problem, can cause this.\n\nUse Scan / repair USB indexes to verify registered content and review the exact NCA/game report. Keep the drive connected. Reinstall only the affected content from a known-good source if needed; do not disable integrity checks."_nx};
        case 4543:
            return {"Unsupported NCA signature-key generation"_nx,
                "The reader does not recognise this NCA signature-key generation.\n\nCheck the firmware/reader version and the package source. Rebuilding indexes or deleting saves does not add support for a new NCA format."_nx};
        case 4523: case 4524: case 4525: case 4526: case 4529: case 4547: case 5326:
            return {"NCA format / patch / compression read failure"_nx,
                "The NCA reader failed while interpreting a filesystem, encryption, patch or compression structure. This result alone does not prove damaged media.\n\nKeep the exact code and fresh USB launch log. Install the matching firmware update before retesting. Index repair cannot add reader-format support."_nx};
        default: return {};
    }
}
}
