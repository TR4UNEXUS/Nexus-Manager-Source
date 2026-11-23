// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/registration_protocol.hpp"
namespace nexus {
// A register collision is not permission to trust or replace an existing NCA.
// The caller verifies its complete size and hash using the pinned destination.
// Only the temporary placeholder created by this copy may be removed here.
template<class Publish,class Verify,class Cleanup>
std::uint32_t FinishCopiedNca(bool& owned,Publish publish,Verify verify,Cleanup cleanup){
    const auto registered=publish();
    if(registered && registered!=registration::ContentAlreadyExists)return registered;
    if(!registered)owned=false;
    const auto checked=verify();if(checked)return checked;
    if(owned){const auto removed=cleanup();if(removed)return removed;owned=false;}
    return 0;
}
}
