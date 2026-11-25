// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <span>
#include <vector>

namespace sphaira::nexusapp {
inline constexpr Result MissingUsbTicket=MAKERESULT(140,205);
inline constexpr Result InvalidUsbTicket=MAKERESULT(505,901);
struct UsbTicketPair { std::vector<u8> ticket,certificate; };
// Original ticket/certificate bytes only. The archive is immutable, checked
// after publication, and scoped to this console's hashed hardware identity.
Result SaveUsbTicketPair(u64 generation,const FsRightsId& rights,
    std::span<const u8> ticket,std::span<const u8> certificate);
// Read-only. Missing and invalid archives are distinct from transport errors.
Result LoadUsbTicketPair(u64 generation,const FsRightsId& rights,UsbTicketPair& out);
// Explicit game import may also read an original NSP pair prepared by the PC.
Result LoadUsbImportTicketPair(u64 generation,const FsRightsId& rights,UsbTicketPair& out);
// Imports exact original bytes; never converts a personalised ticket. Caller
// journals title registration before calling this explicit import operation.
Result ImportUsbTicketPair(u64 generation,const FsRightsId& rights);
}
