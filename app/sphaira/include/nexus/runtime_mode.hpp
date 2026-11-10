// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace nexus::runtime {
inline const char* Executable(bool native,int argc,char** argv){
    // NSO entry has no hbloader argv and its RomFS belongs to the installed
    // process. This identifier must NEVER alias the SD NRO updater target.
    if(native)return "/nexus-standalone";
    return argc>0 && argv && argv[0] && argv[0][0]?argv[0]:nullptr;
}
}
