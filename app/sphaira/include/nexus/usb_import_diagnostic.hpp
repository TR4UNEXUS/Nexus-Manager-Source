// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/localize.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace nexus::usbimport {
// Diagnostic context only. Never include key/ticket bytes or system identity.
inline std::string FailureDetail(std::string_view step,std::uint32_t result,std::string_view existing={}) {
    char code[16]{};std::snprintf(code,sizeof(code),"0x%08X",static_cast<unsigned>(result));
    std::string text(existing);
    if(!text.empty())text+="\n\n";
    text+="Stopped while "_nx;text+=step;text+=" (";text+=code;text+=").";
    return text;
}
}
